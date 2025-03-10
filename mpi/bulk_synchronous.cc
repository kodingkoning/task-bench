/* Copyright 2020 Stanford University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "core.h"

#include "mpi.h"

int main(int argc, char *argv[])
{
  MPI_Init(&argc, &argv);
  int n_ranks, rank;
  MPI_Comm_size(MPI_COMM_WORLD, &n_ranks);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  App app(argc, argv);
  if (rank == 0) app.display();

  std::vector<std::vector<char> > scratch;
  for (auto graph : app.graphs) {
    long first_point = rank * graph.max_width / n_ranks;
    long last_point = (rank + 1) * graph.max_width / n_ranks - 1;
    long n_points = last_point - first_point + 1;

    size_t scratch_bytes = graph.scratch_bytes_per_task;
    scratch.emplace_back(scratch_bytes * n_points);
    TaskGraph::prepare_scratch(scratch.back().data(), scratch.back().size());
  }

  double elapsed_time = 0.0;
  for (int iter = 0; iter < 2; ++iter) {
    MPI_Barrier(MPI_COMM_WORLD);

    double start_time = MPI_Wtime();

    std::vector<MPI_Request> requests;

    for (auto graph : app.graphs) {
      long first_point = rank * graph.max_width / n_ranks;
      long last_point = (rank + 1) * graph.max_width / n_ranks - 1;
      long n_points = last_point - first_point + 1;

      size_t scratch_bytes = graph.scratch_bytes_per_task;
      char *scratch_ptr = scratch[graph.graph_index].data();

      std::vector<int> rank_by_point(graph.max_width);
      std::vector<int> tag_bits_by_point(graph.max_width);
      for (int r = 0; r < n_ranks; ++r) {
        long r_first_point = r * graph.max_width / n_ranks;
        long r_last_point = (r + 1) * graph.max_width / n_ranks - 1;
        for (long p = r_first_point; p <= r_last_point; ++p) {
          rank_by_point[p] = r;
          tag_bits_by_point[p] = p - r_first_point;
          // Has to fit in 7 bits because MPI only guarrantees that
          // tags can use 15 bits.
          assert((tag_bits_by_point[p] & ~0x7F) == 0);
        }
      }

      long max_deps = 0;
      for (long dset = 0; dset < graph.max_dependence_sets(); ++dset) {
        for (long point = first_point; point <= last_point; ++point) {
          long deps = 0;
          for (auto interval : graph.dependencies(dset, point)) {
            deps += interval.second - interval.first + 1;
          }
          max_deps = std::max(max_deps, deps);
        }
      }

      // Create input and output buffers.
      std::vector<std::vector<std::vector<char> > > inputs(n_points);
      std::vector<std::vector<const char *> > input_ptr(n_points);
      std::vector<std::vector<size_t> > input_bytes(n_points);
      std::vector<long> n_inputs(n_points);
      std::vector<std::vector<char> > outputs(n_points);
      for (long point = first_point; point <= last_point; ++point) {
        long point_index = point - first_point;

        auto &point_inputs = inputs[point_index];
        auto &point_input_ptr = input_ptr[point_index];
        auto &point_input_bytes = input_bytes[point_index];

        point_inputs.resize(max_deps);
        point_input_ptr.resize(max_deps);
        point_input_bytes.resize(max_deps);

        for (long dep = 0; dep < max_deps; ++dep) {
          point_inputs[dep].resize(graph.output_bytes_size[0][point]);
          point_input_ptr[dep] = point_inputs[dep].data();
          point_input_bytes[dep] = point_inputs[dep].size();
        }

        auto &point_outputs = outputs[point_index];
        point_outputs.resize(graph.output_bytes_size[0][point]);
      }

      // Cache dependencies.
      std::vector<std::vector<std::vector<std::pair<long, long> > > > dependencies(graph.max_dependence_sets());
      std::vector<std::vector<std::vector<std::pair<long, long> > > > reverse_dependencies(graph.max_dependence_sets());
      for (long dset = 0; dset < graph.max_dependence_sets(); ++dset) {
        dependencies[dset].resize(n_points);
        reverse_dependencies[dset].resize(n_points);

        for (long point = first_point; point <= last_point; ++point) {
          long point_index = point - first_point;

          dependencies[dset][point_index] = graph.dependencies(dset, point);
          reverse_dependencies[dset][point_index] = graph.reverse_dependencies(dset, point);
        }
      }

      for (long timestep = 0; timestep < graph.timesteps; ++timestep) {
        long offset = graph.offset_at_timestep(timestep);
        long width = graph.width_at_timestep(timestep);

        long last_offset = graph.offset_at_timestep(timestep-1);
        long last_width = graph.width_at_timestep(timestep-1);

        long dset = graph.dependence_set_at_timestep(timestep);
        auto &deps = dependencies[dset];
        auto &rev_deps = reverse_dependencies[dset];

        requests.clear();

        for (long point = first_point; point <= last_point; ++point) {
          long point_index = point - first_point;

          auto &point_inputs = inputs[point_index];
          auto &point_n_inputs = n_inputs[point_index];
          auto &point_output = outputs[point_index];
          auto &point_input_ptr = input_ptr[point_index];
          auto &point_input_bytes = input_bytes[point_index];

          auto &point_deps = deps[point_index];
          auto &point_rev_deps = rev_deps[point_index];

          /* Receive */
          point_n_inputs = 0;
          if (point >= offset && point < offset + width) {
            for (auto interval : point_deps) {
              for (long dep = interval.first; dep <= interval.second; ++dep) {
                if (dep < last_offset || dep >= last_offset + last_width) {
                  continue;
                }

                // Use shared memory for on-node data.
                if (first_point <= dep && dep <= last_point) {
                  auto &output = outputs[dep - first_point];
                  point_inputs[point_n_inputs].assign(output.begin(), output.end());
                  point_inputs[point_n_inputs].resize(graph.output_bytes_size[timestep-1][dep]);
                  point_input_ptr[point_n_inputs] = point_inputs[point_n_inputs].data();
                  point_input_bytes[point_n_inputs] = point_inputs[point_n_inputs].size();
                } else {
                  int from = tag_bits_by_point[dep];
                  int to = tag_bits_by_point[point];
                  int tag = (from << 8) | to;
                  MPI_Request req;
                  MPI_Irecv(point_inputs[point_n_inputs].data(),
                            point_inputs[point_n_inputs].size(), MPI_BYTE,
                            rank_by_point[dep], tag, MPI_COMM_WORLD, &req);
                  requests.push_back(req);
                }
                point_n_inputs++;
              }
            }
          }

          /* Send */
          if (point >= last_offset && point < last_offset + last_width) {
            for (auto interval : point_rev_deps) {
              for (long dep = interval.first; dep <= interval.second; dep++) {
                if (dep < offset || dep >= offset + width || (first_point <= dep && dep <= last_point)) {
                  continue;
                }

                int from = tag_bits_by_point[point];
                int to = tag_bits_by_point[dep];
                int tag = (from << 8) | to;
                MPI_Request req;
                MPI_Isend(point_output.data(), point_output.size(), MPI_BYTE,
                          rank_by_point[dep], tag, MPI_COMM_WORLD, &req);
                requests.push_back(req);
              }
            }
          }
        }

        MPI_Waitall(requests.size(), requests.data(), MPI_STATUSES_IGNORE);

        MPI_Barrier(MPI_COMM_WORLD);

        for (long point = std::max(first_point, offset); point <= std::min(last_point, offset + width - 1); ++point) {
          long point_index = point - first_point;

          auto &point_input_ptr = input_ptr[point_index];
          auto &point_input_bytes = input_bytes[point_index];
          auto &point_n_inputs = n_inputs[point_index];
          auto &point_output = outputs[point_index];
          size_t output_point_bytes = graph.output_bytes_size[timestep][point];
          point_output.resize(output_point_bytes);

          graph.execute_point(timestep, point,
                              point_output.data(), point_output.size(),
                              point_input_ptr.data(), point_input_bytes.data(), point_n_inputs,
                              scratch_ptr + scratch_bytes * point_index, scratch_bytes);
        }
      }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    double stop_time = MPI_Wtime();
    elapsed_time = stop_time - start_time;
  }

  if (rank == 0) {
    app.report_timing(elapsed_time);
  }

  MPI_Finalize();
}
