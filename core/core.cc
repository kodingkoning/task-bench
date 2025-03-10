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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <math.h>
#include <vector>
#include <random>

#include "core.h"
#include "core_kernel.h"
#include "core_random.h"

#ifdef DEBUG_CORE
typedef unsigned long long TaskGraphMask;
static std::atomic<TaskGraphMask> has_executed_graph;
#endif

static bool needs_period(DependenceType dtype) {
  return dtype == DependenceType::SPREAD || dtype == DependenceType::RANDOM_NEAREST;
}

void Kernel::execute(long graph_index, long timestep, long point,
                     char *scratch_ptr, size_t scratch_bytes) const
{
  switch(type) {
  case KernelType::EMPTY:
    execute_kernel_empty(*this);
    break;
  case KernelType::BUSY_WAIT:
    execute_kernel_busy_wait(*this);
    break;
  case KernelType::MEMORY_BOUND:
    assert(scratch_ptr != NULL);
    assert(scratch_bytes > 0);
    execute_kernel_memory(*this, scratch_ptr, scratch_bytes, timestep);
    break;
  case KernelType::COMPUTE_DGEMM:
    assert(scratch_ptr != NULL);
    assert(scratch_bytes > 0);
    execute_kernel_dgemm(*this, scratch_ptr, scratch_bytes);
    break;
  case KernelType::MEMORY_DAXPY:
    assert(scratch_ptr != NULL);
    assert(scratch_bytes > 0);
    execute_kernel_daxpy(*this, scratch_ptr, scratch_bytes, timestep);
    break;
  case KernelType::COMPUTE_BOUND:
    execute_kernel_compute(*this);
    break;
  case KernelType::COMPUTE_BOUND2:
    execute_kernel_compute2(*this);
    break;
  case KernelType::IO_BOUND:
    execute_kernel_io(*this);
    break;
  case KernelType::LOAD_IMBALANCE:
    assert(timestep >= 0 && point >= 0);
    execute_kernel_imbalance(*this, graph_index, timestep, point);
    break;
  case KernelType::DIST_IMBALANCE:
    assert(timestep >= 0 && point >= 0);
    execute_kernel_distribution(*this, graph_index, timestep, point);
    break;
  case KernelType::COMPUTE_MEMORY:
    assert(scratch_ptr != NULL);
    assert(scratch_bytes > 0);
    execute_kernel_compute_and_mem(*this, scratch_ptr, scratch_bytes, timestep);
    break;
  default:
    assert(false && "unimplemented kernel type");
  };
}

static const std::map<std::string, KernelType> ktype_by_name = {
  {"empty", KernelType::EMPTY},
  {"busy_wait", KernelType::BUSY_WAIT},
  {"memory_bound", KernelType::MEMORY_BOUND},
  {"compute_dgemm", KernelType::COMPUTE_DGEMM},
  {"memory_daxpy", KernelType::MEMORY_DAXPY},
  {"compute_bound", KernelType::COMPUTE_BOUND},
  {"compute_bound2", KernelType::COMPUTE_BOUND2},
  {"io_bound", KernelType::IO_BOUND},
  {"load_imbalance", KernelType::LOAD_IMBALANCE},
  {"dist_imbalance", KernelType::DIST_IMBALANCE},
  {"compute_and_mem", KernelType::COMPUTE_MEMORY},
};

static const std::map<std::string, DistType> disttype_by_name = {
  {"uniform", DistType::UNIFORM},
  {"normal", DistType::NORMAL},
  {"gamma", DistType::GAMMA},
  {"cauchy", DistType::CAUCHY},
};

static const std::map<std::string, DistParam> distparam_by_name = {
  {"dist-max", DistParam::MAX},
  {"dist-std", DistParam::STD},
  {"dist-beta", DistParam::BETA},
  {"dist-alpha", DistParam::ALPHA},
  // TODO: add explanations of which go with what
};

static std::map<KernelType, std::string> make_name_by_ktype()
{
  std::map<KernelType, std::string> names;

  if (names.empty()) {
    auto types = ktype_by_name;
    for (auto pair : types) {
      names[pair.second] = pair.first;
    }
  }

  return names;
}

static const std::map<KernelType, std::string> name_by_ktype = make_name_by_ktype();

static const std::map<std::string, DependenceType> dtype_by_name = {
  {"trivial", DependenceType::TRIVIAL},
  {"no_comm", DependenceType::NO_COMM},
  {"stencil_1d", DependenceType::STENCIL_1D},
  {"stencil_1d_periodic", DependenceType::STENCIL_1D_PERIODIC},
  {"dom", DependenceType::DOM},
  {"tree", DependenceType::TREE},
  {"fft", DependenceType::FFT},
  {"all_to_all", DependenceType::ALL_TO_ALL},
  {"nearest", DependenceType::NEAREST},
  {"spread", DependenceType::SPREAD},
  {"random_nearest", DependenceType::RANDOM_NEAREST},
  {"random_spread", DependenceType::RANDOM_SPREAD},
};

static std::map<DependenceType, std::string> make_name_by_dtype()
{
  std::map<DependenceType, std::string> names;

  if (names.empty()) {
    auto types = dtype_by_name;
    for (auto pair : types) {
      names[pair.second] = pair.first;
    }
  }

  return names;
}

static std::map<DependenceType, std::string> name_by_dtype = make_name_by_dtype();

long TaskGraph::offset_at_timestep(long timestep) const
{
  if (timestep < 0) {
    return 0;
  }

  switch (dependence) {
  case DependenceType::TRIVIAL:
  case DependenceType::NO_COMM:
  case DependenceType::STENCIL_1D:
  case DependenceType::STENCIL_1D_PERIODIC:
    return 0;
  case DependenceType::DOM:
    return std::max(0L, timestep + max_width - timesteps);
  case DependenceType::TREE:
  case DependenceType::FFT:
  case DependenceType::ALL_TO_ALL:
  case DependenceType::NEAREST:
  case DependenceType::SPREAD:
  case DependenceType::RANDOM_NEAREST:
  case DependenceType::RANDOM_SPREAD:
    return 0;
  default:
    assert(false && "unexpected dependence type");
  };
}

long TaskGraph::width_at_timestep(long timestep) const
{
  if (timestep < 0) {
    return 0;
  }

  switch (dependence) {
  case DependenceType::TRIVIAL:
  case DependenceType::NO_COMM:
  case DependenceType::STENCIL_1D:
  case DependenceType::STENCIL_1D_PERIODIC:
    return max_width;
  case DependenceType::DOM:
    return std::min(max_width,
                    std::min(timestep + 1, timesteps - timestep));
  case DependenceType::TREE:
    return std::min(max_width, 1L << std::min(timestep, 62L));
  case DependenceType::FFT:
  case DependenceType::ALL_TO_ALL:
  case DependenceType::NEAREST:
  case DependenceType::SPREAD:
  case DependenceType::RANDOM_NEAREST:
  case DependenceType::RANDOM_SPREAD:
    return max_width;
  default:
    assert(false && "unexpected dependence type");
  };
}


long TaskGraph::max_dependence_sets() const
{
  switch (dependence) {
  case DependenceType::TRIVIAL:
  case DependenceType::NO_COMM:
  case DependenceType::STENCIL_1D:
  case DependenceType::STENCIL_1D_PERIODIC:
  case DependenceType::DOM:
  case DependenceType::TREE:
    return 1;
  case DependenceType::FFT:
    return (long)ceil(log2(max_width));
  case DependenceType::ALL_TO_ALL:
  case DependenceType::NEAREST:
    return 1;
  case DependenceType::SPREAD:
  case DependenceType::RANDOM_NEAREST:
  case DependenceType::RANDOM_SPREAD:
    return period;
  default:
    assert(false && "unexpected dependence type");
  };
}

long TaskGraph::timestep_period() const
{
  // For all the dependence types currently defined, the pattern
  // repeats with a period equal to the number of timesteps.
  return max_dependence_sets();
}

long TaskGraph::dependence_set_at_timestep(long timestep) const
{
  switch (dependence) {
  case DependenceType::TRIVIAL:
  case DependenceType::NO_COMM:
  case DependenceType::STENCIL_1D:
  case DependenceType::STENCIL_1D_PERIODIC:
  case DependenceType::DOM:
  case DependenceType::TREE:
    return 0;
  case DependenceType::FFT:
    return (timestep + max_dependence_sets() - 1) % max_dependence_sets();
  case DependenceType::ALL_TO_ALL:
  case DependenceType::NEAREST:
    return 0;
  case DependenceType::SPREAD:
  case DependenceType::RANDOM_NEAREST:
  case DependenceType::RANDOM_SPREAD:
    return timestep % max_dependence_sets();
  default:
    assert(false && "unexpected dependence type");
  };
}

std::vector<std::pair<long, long> > TaskGraph::reverse_dependencies(long dset, long point) const
{
  size_t count = num_reverse_dependencies(dset, point);
  std::vector<std::pair<long, long> > deps(count);
  size_t actual_count = reverse_dependencies(dset, point, deps.data());
  assert(actual_count <= count);
  deps.resize(actual_count);
  return deps;
}

size_t TaskGraph::reverse_dependencies(long dset, long point, std::pair<long, long> *deps) const
{
  switch (dependence) {
  case DependenceType::TRIVIAL:
    return 0;
  case DependenceType::NO_COMM:
    deps[0] = std::pair<long, long>(point, point);
    return 1;
  case DependenceType::STENCIL_1D:
    deps[0] = std::pair<long, long>(std::max(0L, point-1),
                                    std::min(point+1, max_width-1));
    return 1;
  case DependenceType::STENCIL_1D_PERIODIC:
    {
      size_t idx = 0;
      deps[idx++] = std::pair<long, long>(std::max(0L, point-1),
                                          std::min(point+1, max_width-1));
      if (point-1 < 0) { // Wrap around negative case
        deps[idx++] = std::pair<long, long>(max_width-1, max_width-1);
      }
      if (point+1 >= max_width) { // Wrap around positive case
        deps[idx++] = std::pair<long, long>(0, 0);
      }
      return idx;
    }
  case DependenceType::DOM:
    deps[0] = std::pair<long, long>(point, std::min(max_width-1, point+1));
    return 1;
  case DependenceType::TREE:
    {
      long child1 = point*2;
      long child2 = point*2 + 1;
      if (child1 < max_width && child2 < max_width) {
        deps[0] = std::pair<long, long>(child1, child2);
        return 1;
      } else if (child1 < max_width) {
        deps[0] = std::pair<long, long>(child1, child1);
        return 1;
      }
      return 0;
    }
  case DependenceType::FFT:
    {
      size_t idx = 0;
      long d1 = point - (1 << dset);
      long d2 = point + (1 << dset);
      if (d1 >= 0) {
        deps[idx++] = std::pair<long, long>(d1, d1);
      }
      deps[idx++] = std::pair<long, long>(point, point);
      if (d2 < max_width) {
        deps[idx++] = std::pair<long, long>(d2, d2);
      }
      return idx;
    }
  case DependenceType::ALL_TO_ALL:
    deps[0] = std::pair<long, long>(0, max_width-1);
    return 1;
  case DependenceType::NEAREST:
    if (radix > 0) {
      deps[0] = std::pair<long, long>(std::max(0L, point - (radix-1)/2),
                                      std::min(point + radix/2,
                                               max_width-1));
      return 1;
    }
    return 0;
  case DependenceType::SPREAD:
    for (long i = 0; i < radix; ++i) {
      long dep = (point - i*max_width/radix - (i>0 ? dset : 0)) % max_width;
      if (dep < 0) dep += max_width;
      deps[i] = std::pair<long, long>(dep, dep);
    }
    return radix;
  case DependenceType::RANDOM_NEAREST:
    {
      size_t idx = 0;
      long run_start = -1;
      long i, last_i;
      for (i = std::max(0L, point - (radix-1)/2),
             last_i = std::min(point + radix/2, max_width-1);
           i <= last_i; ++i) {
        // Figure out whether we're including this dependency or not.
        const long hash_value[5] = {graph_index, radix, dset, point, i};
        double value = random_uniform(&hash_value[0], sizeof(hash_value));
        bool include = value < fraction_connected || (radix > 0 && i == point);

        if (include) {
          if (run_start < 0) {
            run_start = i;
          }
        } else {
          if (run_start >= 0) {
            deps[idx++] = std::pair<long, long>(run_start, i-1);
          }
          run_start = -1;
        }
      }
      if (run_start >= 0) {
        deps[idx++] = std::pair<long, long>(run_start, i-1);
      }
      return idx;
    }
    break;
  default:
    assert(false && "unexpected dependence type");
  };

  return SIZE_MAX;
}

size_t TaskGraph::num_reverse_dependencies(long dset, long point) const
{
  switch (dependence) {
  case DependenceType::TRIVIAL:
    return 0;
  case DependenceType::NO_COMM:
  case DependenceType::STENCIL_1D:
    return 1;
  case DependenceType::STENCIL_1D_PERIODIC:
    return max_width > 1 ? 2 : 3;
  case DependenceType::DOM:
  case DependenceType::TREE:
    return 1;
  case DependenceType::FFT:
    return 3;
  case DependenceType::ALL_TO_ALL:
    return 1;
  case DependenceType::NEAREST:
    return radix > 0 ? 1 : 0;
  case DependenceType::SPREAD:
  case DependenceType::RANDOM_NEAREST:
    return radix;
  default:
    assert(false && "unexpected dependence type");
  };

  return SIZE_MAX;
}

std::vector<std::pair<long, long> > TaskGraph::dependencies(long dset, long point) const
{
  size_t count = num_dependencies(dset, point);
  std::vector<std::pair<long, long> > deps(count);
  size_t actual_count = dependencies(dset, point, deps.data());
  assert(actual_count <= count);
  deps.resize(actual_count);
  return deps;
}

size_t TaskGraph::dependencies(long dset, long point, std::pair<long, long> *deps) const
{
  switch (dependence) {
  case DependenceType::TRIVIAL:
    return 0;
  case DependenceType::NO_COMM:
    deps[0] = std::pair<long, long>(point, point);
    return 1;
  case DependenceType::STENCIL_1D:
    deps[0] = std::pair<long, long>(std::max(0L, point-1),
                                    std::min(point+1, max_width-1));
    return 1;
  case DependenceType::STENCIL_1D_PERIODIC:
    {
      size_t idx = 0;
      deps[idx++] = std::pair<long, long>(std::max(0L, point-1),
                                          std::min(point+1, max_width-1));
      if (point-1 < 0) { // Wrap around negative case
        deps[idx++] = std::pair<long, long>(max_width-1, max_width-1);
      }
      if (point+1 >= max_width) { // Wrap around positive case
        deps[idx++] = std::pair<long, long>(0, 0);
      }
      return idx;
    }
  case DependenceType::DOM:
    deps[0] = std::pair<long, long>(std::max(0L, point-1), point);
    return 1;
  case DependenceType::TREE:
    {
      long parent = point/2;
      deps[0] = std::pair<long, long>(parent, parent);
      return 1;
    }
  case DependenceType::FFT:
    {
      size_t idx = 0;
      long d1 = point - (1 << dset);
      long d2 = point + (1 << dset);
      if (d1 >= 0) {
        deps[idx++] = std::pair<long, long>(d1, d1);
      }
      deps[idx++] = std::pair<long, long>(point, point);
      if (d2 < max_width) {
        deps[idx++] = std::pair<long, long>(d2, d2);
      }
      return idx;
    }
  case DependenceType::ALL_TO_ALL:
    deps[0] = std::pair<long, long>(0, max_width-1);
    return 1;
  case DependenceType::NEAREST:
    if (radix > 0) {
      deps[0] = std::pair<long, long>(std::max(0L, point - radix/2),
                                      std::min(point + (radix-1)/2,
                                               max_width-1));
      return 1;
    }
    return 0;
  case DependenceType::SPREAD:
    for (long i = 0; i < radix; ++i) {
      long dep = (point + i*max_width/radix + (i>0 ? dset : 0)) % max_width;
      deps[i] = std::pair<long, long>(dep, dep);
    }
    return radix;
  case DependenceType::RANDOM_NEAREST:
    {
      size_t idx = 0;
      long run_start = -1;
      long i, last_i;
      for (i = std::max(0L, point - radix/2),
             last_i = std::min(point + (radix-1)/2, max_width-1);
           i <= last_i; ++i) {
        // Figure out whether we're including this dependency or not.
        const long hash_value[5] = {graph_index, radix, dset, i, point};
        double value = random_uniform(&hash_value[0], sizeof(hash_value));
        bool include = value < fraction_connected || (radix > 0 && i == point);

        if (include) {
          if (run_start < 0) {
            run_start = i;
          }
        } else {
          if (run_start >= 0) {
            deps[idx++] = std::pair<long, long>(run_start, i-1);
          }
          run_start = -1;
        }
      }
      if (run_start >= 0) {
        deps[idx++] = std::pair<long, long>(run_start, i-1);
      }
      return idx;
    }
    break;
  default:
    assert(false && "unexpected dependence type");
  };

  return SIZE_MAX;
}

size_t TaskGraph::num_dependencies(long dset, long point) const
{
  switch (dependence) {
  case DependenceType::TRIVIAL:
    return 0;
  case DependenceType::NO_COMM:
  case DependenceType::STENCIL_1D:
    return 1;
  case DependenceType::STENCIL_1D_PERIODIC:
    return max_width > 1 ? 2 : 3;
  case DependenceType::DOM:
  case DependenceType::TREE:
    return 1;
  case DependenceType::FFT:
    return 3;
  case DependenceType::ALL_TO_ALL:
    return 1;
  case DependenceType::NEAREST:
    return radix > 0 ? 1 : 0;
  case DependenceType::SPREAD:
  case DependenceType::RANDOM_NEAREST:
    return radix;
  default:
    assert(false && "unexpected dependence type");
  };

  return SIZE_MAX;
}

#define MAGIC_VALUE UINT64_C(0x5C4A7C8B) // can you read it? it says "SCRATCHB" (kinda)

void TaskGraph::execute_point(long timestep, long point,
                              char *output_ptr, size_t output_bytes,
                              const char **input_ptr, const size_t *input_bytes,
                              size_t n_inputs,
                              char *scratch_ptr, size_t scratch_bytes) const
{
#ifdef DEBUG_CORE
  // Validate graph_index
  assert(graph_index >= 0 && graph_index < sizeof(TaskGraphMask)*8);
  has_executed_graph |= 1 << graph_index;
#endif

  // Validate timestep and point
  assert(0 <= timestep && timestep < timesteps);

  long offset = offset_at_timestep(timestep);
  long width = width_at_timestep(timestep);
  assert(offset <= point && point < offset+width);

  long last_offset = offset_at_timestep(timestep-1);
  long last_width = width_at_timestep(timestep-1);

  // Validate input
  {
    size_t idx = 0;
    long dset = dependence_set_at_timestep(timestep);
    size_t max_deps = num_dependencies(dset, point);
    std::pair<long, long> *deps = reinterpret_cast<std::pair<long, long> *>(alloca(sizeof(std::pair<long, long>) * max_deps));
    size_t num_deps = dependencies(dset, point, deps);
    for (size_t span = 0; span < num_deps; span++) {
      for (long dep = deps[span].first; dep <= deps[span].second; dep++) {
        if (last_offset <= dep && dep < last_offset + last_width) {
          assert(idx < n_inputs);

          //assert(input_bytes[idx] == output_bytes_size[timestep][point]);//output_bytes_per_task);
	  //printf("input_bytes[%d], %ld / %ld\n",idx, input_bytes[idx],sizeof(std::pair<long,long>));
          assert(input_bytes[idx] >= sizeof(std::pair<long, long>));

          const std::pair<long, long> *input = reinterpret_cast<const std::pair<long, long> *>(input_ptr[idx]);
	  //printf("input_ptr[%d]: %s\n", idx, *input_ptr[idx]);
	  //printf("output_ptr[%d]: %s\n", idx, output_ptr);
	  //printf("input_bytes[%d]: %ld/%ld\n", idx, input_bytes[idx],sizeof(std::pair<long,long>));
          for (size_t i = 0; i < input_bytes[idx]/sizeof(std::pair<long, long>); ++i) {
//#ifdef DEBUG_CORE
            if (input[i].first != timestep - 1 || input[i].second != dep) {
              printf("ERROR: Task Bench detected corrupted value in task (graph %ld timestep %ld point %ld) input %ld\n  At position %lu within the buffer, expected value (timestep %ld point %ld) but got (timestep %ld point %ld)\n",
                     graph_index, timestep, point, idx,
                     i, timestep - 1, dep, input[i].first, input[i].second);
              fflush(stdout);
            }
//#endif
	    //printf("input[%d]\n", i);
	    //printf("timestep-1 %ld input.first %ld\n", timestep-1, input[i].first);
	    //printf("dep %ld, input.second %ld\n", dep, input[i].second);
            assert(input[i].first == timestep - 1);
            assert(input[i].second == dep);
          }
          idx++;
        }
      }
    }
    // FIXME (Elliott): Legion is currently passing in uninitialized
    // memory for dependencies outside of the last offset/width.
    // assert(idx == n_inputs);
  }

  // Validate output
  //printf("my/model: %ld/%ld\n", output_bytes, output_bytes_size[timestep][point]);
  //assert(output_bytes == output_bytes_size[timestep][point]);//output_bytes_per_task);
  //assert(output_bytes == output_bytes_per_task);
  //printf("output_bytes: %ld / %ld\n",output_bytes,sizeof(std::pair<long,long>));
  assert(output_bytes >= sizeof(std::pair<long, long>));

  // Generate output
  std::pair<long, long> *output = reinterpret_cast<std::pair<long, long> *>(output_ptr);
  for (size_t i = 0; i < output_bytes/sizeof(std::pair<long, long>); ++i) {
    output[i].first = timestep;
    output[i].second = point;
  }

  // Validate scratch
  assert(scratch_bytes == scratch_bytes_per_task);
  if (scratch_bytes > 0) {
    uint64_t *scratch = reinterpret_cast<uint64_t *>(scratch_ptr);
    assert(*scratch == MAGIC_VALUE);
  }

  // Execute kernel
  Kernel k(kernel);
  k.execute(graph_index, timestep, point, scratch_ptr, scratch_bytes);
}

void TaskGraph::prepare_scratch(char *scratch_ptr, size_t scratch_bytes)
{
  assert(scratch_bytes % sizeof(uint64_t) == 0);
  uint64_t *base_ptr = reinterpret_cast<uint64_t *>(scratch_ptr);
  for (long i = 0; i < scratch_bytes/sizeof(uint64_t); ++i) {
    base_ptr[i] = MAGIC_VALUE;
  }
}

static TaskGraph default_graph(long graph_index)
{
  TaskGraph graph;

  graph.graph_index = graph_index;
  graph.timesteps = 4;
  graph.max_width = 4;
  graph.dependence = DependenceType::TRIVIAL;
  graph.radix = 3;
  graph.period = -1;
  graph.output_case=0;
  graph.fraction_connected = 0.25;
  graph.kernel = {KernelType::EMPTY, 0, 16, 0.0};
  graph.output_bytes_per_task = sizeof(std::pair<long, long>);
  graph.scratch_bytes_per_task = 0;
  graph.nb_fields = 0;
  graph.onormal_mu = 2;
  graph.onormal_std = 2;
  graph.ogamma_alpha = 2;
  graph.ogamma_beta = 2;
  //vector<vector<size_t>>* graph.output_bytes;

  return graph;
}

static void needs_argument(int i, int argc, const char *flag) {
  if (i+1 >= argc) {
    fprintf(stderr, "error: Flag \"%s\" requires an argument\n", flag);
    abort();
  }
}

#define STEPS_FLAG "-steps"
#define WIDTH_FLAG "-width"
#define TYPE_FLAG "-type"
#define RADIX_FLAG "-radix"
#define PERIOD_FLAG "-period"
#define FRACTION_FLAG "-fraction"
#define AND_FLAG "-and"

#define KERNEL_FLAG "-kernel"
#define ITER_FLAG "-iter"
#define OUTPUT_FLAG "-output"
#define SCRATCH_FLAG "-scratch"
#define SAMPLE_FLAG "-sample"
#define IMBALANCE_FLAG "-imbalance"
#define MEM_FRAC_FLAG "-mem-fraction"
#define DIST_FLAG "-dist"

// distribution flags. All accept same datatype as result, which is a long
#define DIST_MAX_FLAG "-dist-max" // for uniform
#define DIST_STD_FLAG "-dist-std" // for normal
#define DIST_BETA_FLAG "-dist-beta" // for gamma
#define DIST_ALPHA_FLAG "-dist-alpha" // for gamma

#define NODES_FLAG "-nodes"
#define SKIP_GRAPH_VALIDATION_FLAG "-skip-graph-validation"
#define FIELD_FLAG "-field"

#define ODIST_FLAG "-output-dist"
#define ONORMAL_MEAN_FLAG "-output-mean"
#define ONORMAL_STD_FLAG "-output-std"
#define ONORMAL_RANDOM_FLAG "-output-random"
#define OUNIFORM_FLAG "-output-uniform"
#define OGAMMA_ALPHA_FLAG "-output-gamma-a"
#define OGAMMA_BETA_FLAG "-output-gamma-b"
#define OCASE_FLAG "-output-case"


static void show_help_message(int argc, char **argv) {
  printf("%s: A Task Benchmark\n", argc > 0 ? argv[0] : "task_bench");

  printf("\nGeneral options:\n");
  printf("  %-18s show this help message and exit\n", "-h");
  printf("  %-18s number of nodes to use for estimating transfer statistics\n", NODES_FLAG);
  printf("  %-18s enable verbose output\n", "-v");
  printf("  %-18s enable extra verbose output\n", "-vv");

  printf("\nOptions for configuring the task graph:\n");
  printf("  %-18s height of task graph\n", STEPS_FLAG " [INT]");
  printf("  %-18s width of task graph\n", WIDTH_FLAG " [INT]");
  printf("  %-18s dependency pattern (see available list below)\n", TYPE_FLAG " [DEP]");
  printf("  %-18s radix of dependency pattern (only for nearest, spread, and random)\n", RADIX_FLAG " [INT]");
  printf("  %-18s period of dependency pattern (only for spread and random)\n", PERIOD_FLAG " [INT]");
  printf("  %-18s fraction of connected dependencies (only for random)\n", FRACTION_FLAG " [FLOAT]");
  printf("  %-18s start configuring next task graph\n", AND_FLAG);

  printf("\nOptions for configuring kernels:\n");
  printf("  %-18s kernel type (see available list below)\n", KERNEL_FLAG " [KERNEL]");
  printf("  %-18s number of iterations\n", ITER_FLAG " [INT]");
  printf("  %-18s output bytes per task\n", OUTPUT_FLAG " [INT]");
  printf("  %-18s scratch bytes per task (only for memory-bound kernel)\n", SCRATCH_FLAG " [INT]");
  printf("  %-18s number of samples (only for memory-bound kernel)\n", SAMPLE_FLAG " [INT]");
  printf("  %-18s amount of load imbalance\n", IMBALANCE_FLAG " [FLOAT]");
  printf("  %-18s fraction of memory iterations (only for memory-and-compute\n", MEM_FRAC_FLAG " [FLOAT]");
  printf("  %-18s distribution type (see available list below)\n", DIST_FLAG " [DIST]");

  printf("\nSupported dependency patterns:\n");
  for (auto dtype : dtype_by_name) {
    printf("  %s\n", dtype.first.c_str());
  }

  printf("\nSupported kernel types:\n");
  for (auto ktype : ktype_by_name) {
    printf("  %s\n", ktype.first.c_str());
  }

  printf("\nSupported distribution types:\n");
  for (auto disttype : disttype_by_name) {
    printf(" %s\n", disttype.first.c_str());
  }

  printf("\nSupported distribution parameters:\n");
  for (auto distparam : distparam_by_name) {
    printf(" %s\n", distparam.first.c_str());
    // TODO: add explanation of how to use the distribution parameters
  }

  printf("\nLess frequently used options:\n");
  printf("  %-18s number of fields (optimization for certain task bench implementations)\n", FIELD_FLAG " [INT]");
  printf("  %-18s skip task graph validation\n", SKIP_GRAPH_VALIDATION_FLAG);
}

App::App(int argc, char **argv)
  : nodes(0)
  , verbose(0)
  , enable_graph_validation(true)
{
  TaskGraph graph = default_graph(graphs.size());

  // Parse command line
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-h")) {
      show_help_message(argc, argv);
      exit(0);
    }

    if (!strcmp(argv[i], NODES_FLAG)) {
      needs_argument(i, argc, NODES_FLAG);
      long value = atol(argv[++i]);
      if (value <= 0) {
        fprintf(stderr, "error: Invalid flag \"" NODES_FLAG " %ld\" must be > 0\n", value);
        abort();
      }
      nodes = value;
    }

    if (!strcmp(argv[i], "-v")) {
      verbose++;
    }

    if (!strcmp(argv[i], "-vv")) {
      verbose += 2;
    }

    if (!strcmp(argv[i], SKIP_GRAPH_VALIDATION_FLAG)) {
      enable_graph_validation = false;
    }

    if (!strcmp(argv[i], STEPS_FLAG)) {
      needs_argument(i, argc, STEPS_FLAG);
      long value = atol(argv[++i]);
      if (value <= 0) {
        fprintf(stderr, "error: Invalid flag \"" STEPS_FLAG " %ld\" must be > 0\n", value);
        abort();
      }
      graph.timesteps = value;
    }

    if (!strcmp(argv[i], WIDTH_FLAG)) {
      needs_argument(i, argc, WIDTH_FLAG);
      long value = atol(argv[++i]);
      if (value <= 0) {
        fprintf(stderr, "error: Invalid flag \"" WIDTH_FLAG " %ld\" must be > 0\n", value);
        abort();
      }
      graph.max_width = value;
    }

    if (!strcmp(argv[i], TYPE_FLAG)) {
      needs_argument(i, argc, TYPE_FLAG);
      auto name = argv[++i];
      auto type = dtype_by_name.find(name);
      if (type == dtype_by_name.end()) {
        fprintf(stderr, "error: Invalid flag \"-type %s\"\n", name);
        abort();
      }
      graph.dependence = type->second;
    }

    if (!strcmp(argv[i], DIST_FLAG)) {
      needs_argument(i, argc, DIST_FLAG);
      auto name = argv[++i];
      auto type = disttype_by_name.find(name);
      if (type == disttype_by_name.end()) {
        fprintf(stderr, "error: Invalid flag \"-dist %s\"\n", name);
        abort();
      }
      graph.kernel.dist.type = type->second;
    }

    if (!strcmp(argv[i], RADIX_FLAG)) {
      needs_argument(i, argc, RADIX_FLAG);
      long value = atol(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" RADIX_FLAG " %ld\" must be >= 0\n", value);
        abort();
      }
      graph.radix = value;
    }

    if (!strcmp(argv[i], PERIOD_FLAG)) {
      needs_argument(i, argc, PERIOD_FLAG);
      long value = atol(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" PERIOD_FLAG " %ld\" must be >= 0\n", value);
        abort();
      }
      graph.period = value;
    }

    if (!strcmp(argv[i], FRACTION_FLAG)) {
      needs_argument(i, argc, FRACTION_FLAG);
      double value = atof(argv[++i]);
      if (value < 0 || value > 1) {
        fprintf(stderr, "error: Invalid flag \"" FRACTION_FLAG " %f\" must be >= 0 and <= 1\n", value);
        abort();
      }
      graph.fraction_connected = value;
    }

    if (!strcmp(argv[i], KERNEL_FLAG)) {
      needs_argument(i, argc, KERNEL_FLAG);
      auto name = argv[++i];
      auto type = ktype_by_name.find(name);
      if (type == ktype_by_name.end()) {
        fprintf(stderr, "error: Invalid flag \"" KERNEL_FLAG " %s\"\n", name);
        abort();
      }
      graph.kernel.type = type->second;
    }

    if (!strcmp(argv[i], ITER_FLAG)) {
      needs_argument(i, argc, ITER_FLAG);
      long value = atol(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" ITER_FLAG " %ld\" must be >= 0\n", value);
        abort();
      }
      graph.kernel.iterations = value;
    }

    if (!strcmp(argv[i], OUTPUT_FLAG)) {
      needs_argument(i, argc, OUTPUT_FLAG);
      long value  = atol(argv[++i]);
      if (value < sizeof(std::pair<long, long>)) {
        fprintf(stderr, "error: Invalid flag \"" OUTPUT_FLAG " %ld\" must be >= %lu\n",
                value, sizeof(std::pair<long, long>));
        abort();
      }
      graph.output_bytes_per_task = value;
    }

    if (!strcmp(argv[i], SCRATCH_FLAG)) {
      needs_argument(i, argc, SCRATCH_FLAG);
      long value  = atol(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" SCRATCH_FLAG " %ld\" must be >= 0\n", value);
        abort();
      }
      graph.scratch_bytes_per_task = value;
    }

    if (!strcmp(argv[i], SAMPLE_FLAG)) {
      needs_argument(i, argc, SAMPLE_FLAG);
      int value  = atoi(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" SAMPLE_FLAG " %d\" must be >= 0\n", value);
        abort();
      }
      graph.kernel.samples = value;
    }

    if (!strcmp(argv[i], IMBALANCE_FLAG)) {
      needs_argument(i, argc, IMBALANCE_FLAG);
      double value = atof(argv[++i]);
      if (value < 0 || value > 2) {
        fprintf(stderr, "error: Invalid flag \"" IMBALANCE_FLAG " %f\" must be >= 0 and <= 2\n", value);
        abort();
      }
      graph.kernel.imbalance = value;
    }

    if (!strcmp(argv[i], MEM_FRAC_FLAG)) {
      needs_argument(i, argc, MEM_FRAC_FLAG);
      double value = atof(argv[++i]);
      if (value < 0 || value > 1) {
        fprintf(stderr, "error: Invalid flag \"" MEM_FRAC_FLAG " %f\" must be >= 0 and <= 1\n", value);
        abort();
      }
      graph.kernel.fraction_mem = value;
    }

    if (!strcmp(argv[i], FIELD_FLAG)) {
      needs_argument(i, argc, FIELD_FLAG);
      int value  = atoi(argv[++i]);
      if (value <= 0) {
        fprintf(stderr, "error: Invalid flag \"" FIELD_FLAG " %d\" must be > 1\n", value);
        abort();
      }
      graph.nb_fields = value;
    }

    if (!strcmp(argv[i], OCASE_FLAG)) {
      needs_argument(i, argc, OCASE_FLAG);
      int value  = atoi(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" OCASE_FLAG " %d\" must be >= 0\n", value);
        abort();
      }
      graph.output_case = value;
    }

    if (!strcmp(argv[i], ONORMAL_MEAN_FLAG)) {
      needs_argument(i, argc, ONORMAL_MEAN_FLAG);
      int value  = atoi(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" ONORMAL_MEAN_FLAG " %d\" must be > 0\n", value);
        abort();
      }
      graph.onormal_mu = value;
    }

    if (!strcmp(argv[i], ONORMAL_STD_FLAG)) {
      needs_argument(i, argc, ONORMAL_STD_FLAG);
      int value  = atoi(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" ONORMAL_STD_FLAG " %d\" must be > 0\n", value);
        abort();
      }
      graph.onormal_std = value;
    }

    if (!strcmp(argv[i], OGAMMA_ALPHA_FLAG)) {
      needs_argument(i, argc, OGAMMA_ALPHA_FLAG);
      int value  = atoi(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" OGAMMA_ALPHA_FLAG " %d\" must be > 0\n", value);
        abort();
      }
      graph.ogamma_alpha = value;
    }

    if (!strcmp(argv[i], OGAMMA_BETA_FLAG)) {
      needs_argument(i, argc, OGAMMA_BETA_FLAG);
      int value  = atoi(argv[++i]);
      if (value < 0) {
        fprintf(stderr, "error: Invalid flag \"" OGAMMA_BETA_FLAG " %d\" must be > 0\n", value);
        abort();
      }
      graph.ogamma_beta = value;
    }

    if (!strcmp(argv[i], AND_FLAG)) {
      // Hack: set default value of period for random graph
      if (graph.period < 0) {
        graph.period = needs_period(graph.dependence) ? 3 : 0;
      }
      graphs.push_back(graph);
      graph = default_graph(graphs.size());
    }

    if (!strcmp(argv[i], DIST_MAX_FLAG)) {
      needs_argument(i, argc, DIST_MAX_FLAG);
      long value = atol(argv[++i]);
      if (value <= 0) {
        fprintf(stderr, "error: Invalid flag \"" DIST_MAX_FLAG " %ld\" must be > 0\n", value);
        abort();
      }
      graph.kernel.dist.max = value;
    }

    if (!strcmp(argv[i], DIST_STD_FLAG)) {
      needs_argument(i, argc, DIST_STD_FLAG);
      long value = atol(argv[++i]);
      if (value <= 0) {
        fprintf(stderr, "error: Invalid flag \"" DIST_STD_FLAG " %ld\" must be > 0\n", value);
        abort();
      }
      graph.kernel.dist.std = value;
    }

    if (!strcmp(argv[i], DIST_BETA_FLAG)) {
      needs_argument(i, argc, DIST_BETA_FLAG);
      double value = atof(argv[++i]);
      if (value <= 0) {
        fprintf(stderr, "error: Invalid flag \"" DIST_BETA_FLAG " %f\" must be > 0\n", value);
        abort();
      }
      graph.kernel.dist.b = value;
    }

    if (!strcmp(argv[i], DIST_ALPHA_FLAG)) {
      needs_argument(i, argc, DIST_ALPHA_FLAG);
      long value = atol(argv[++i]);
      if (value <= 0) {
        fprintf(stderr, "error: Invalid flag \"" DIST_ALPHA_FLAG " %ld\" must be > 0\n", value);
        abort();
      }
      graph.kernel.dist.a = value;
    }

  }

  if(graph.kernel.type == KernelType::DIST_IMBALANCE) {
    if (graph.kernel.dist.type == DistType::UNIFORM && graph.kernel.dist.max < graph.kernel.iterations) {
      fprintf(stderr, "error: Uniform distribution requires a maximum that is greater than the minimum given to -iter.\n");
      abort();
    }
    if (graph.kernel.dist.type == DistType::GAMMA && graph.kernel.dist.a <= 0) {
      fprintf(stderr, "error: Gamma distribution requires a value of a that is greater than 0.\n");
      abort();
    }
    if (graph.kernel.dist.type == DistType::NORMAL && graph.kernel.dist.std <= 0) {
      fprintf(stderr, "error: Normal distribution requires a standard deviation that is greater than 0.\n");
      abort();
    }
    if (graph.kernel.dist.type == DistType::CAUCHY && graph.kernel.dist.b <= 0) {
      fprintf(stderr, "error: Cauchy distribution requires a value of b that is greater than 0.\n");
      abort();
    }
  }

  if (graph.period < 0) {
    graph.period = needs_period(graph.dependence) ? 3 : 0;
  }

  // Dynamic array for output_bytes
  graph.output_bytes_size = new size_t*[graph.timesteps];
  for(int i = 0; i < graph.timesteps; i++)
    graph.output_bytes_size[i] = new size_t[graph.max_width];

  allocate_bytes(graph);

  /*for(int t = 0; t< graph.timesteps; t++){
    for(int i = 0; i< graph.max_width; i++){
      printf("t %d i %d; outputsize: %ld\n",t, i, graph.output_bytes_size[t][i]);
    }
  }*/



  graphs.push_back(graph);

  // check nb_fields, if not set by user, set it to timesteps
  for (int j = 0; j < graphs.size(); j++) {
    TaskGraph &g = graphs[j];
    if (g.nb_fields == 0) {
      g.nb_fields = g.timesteps;
    }
  }

  check();
}

void App::check() const
{
#ifdef DEBUG_CORE
  if (graphs.size() >= sizeof(TaskGraphMask)*8) {
    fprintf(stderr, "error: Can only execute up to %lu task graphs\n", sizeof(TaskGraphMask)*8);
    abort();
  }
#endif

  // Validate task graph is well-formed
  for (auto g : graphs) {
    if (needs_period(g.dependence) && g.period == 0) {
      fprintf(stderr, "error: Graph type \"%s\" requires a non-zero period (specify with -period)\n",
              name_by_dtype.at(g.dependence).c_str());
      abort();
    } else if (!needs_period(g.dependence) && g.period != 0) {
      fprintf(stderr, "error: Graph type \"%s\" does not support user-configurable period\n",
              name_by_dtype.at(g.dependence).c_str());
      abort();
    }

    // This is required to avoid wrapping around with later dependence sets.
    long spread = (g.max_width + g.radix - 1) / g.radix;
    if (g.dependence == DependenceType::SPREAD && g.period > spread) {
      fprintf(stderr, "error: Graph type \"%s\" requires a period that is at most %ld\n",
              name_by_dtype.at(g.dependence).c_str(), spread);
      abort();
    }

    for (long t = 0; t < g.timesteps; ++t) {
      long offset = g.offset_at_timestep(t);
      long width = g.width_at_timestep(t);
      assert(offset >= 0);
      assert(width >= 0);
      assert(offset + width <= g.max_width);

      long dset = g.dependence_set_at_timestep(t);
      assert(dset >= 0 && dset <= g.max_dependence_sets());
    }
    for (long dset = 0; dset < g.max_dependence_sets(); ++dset) {
      std::map<long, std::set<long> > materialized_deps;
      for (long point = 0; point < g.max_width; ++point) {
        auto deps = g.dependencies(dset, point);
        for (auto dep : deps) {
          for (long dp = dep.first; dp <= dep.second; ++dp) {
            assert(materialized_deps[point].count(dp) == 0); // No duplicates
            materialized_deps[point].insert(dp);
          }
        }
      }

      // Reverse dependencies mirror dependencies
      for (long point = 0; point < g.max_width; ++point) {
        auto rdeps = g.reverse_dependencies(dset, point);
        for (auto rdep : rdeps) {
          for (long rdp = rdep.first; rdp <= rdep.second; ++rdp) {
            assert(materialized_deps[rdp].count(point) == 1);
          }
        }
      }
    }
  }
}

void App::display() const
{
  printf("Running Task Benchmark\n");
  printf("  Configuration:\n");
  int i = 0;
  for (auto g : graphs) {
    ++i;

    printf("    Task Graph %d:\n", i);
    printf("      Time Steps: %ld\n", g.timesteps);
    printf("      Max Width: %ld\n", g.max_width);
    printf("      Dependence Type: %s\n", name_by_dtype.at(g.dependence).c_str());
    printf("      Radix: %ld\n", g.radix);
    printf("      Period: %ld\n", g.period);
    printf("      Fraction Connected: %f\n", g.fraction_connected);
    printf("      Kernel:\n");
    printf("        Type: %s\n", name_by_ktype.at(g.kernel.type).c_str());
    printf("        Iterations: %ld\n", g.kernel.iterations);
    printf("        Samples: %d\n", g.kernel.samples);
    printf("        Imbalance: %f\n", g.kernel.imbalance);
    printf("      Output Bytes: %lu\n", g.output_bytes_per_task);
    printf("      Scratch Bytes: %lu\n", g.scratch_bytes_per_task);

    if (verbose > 0) {
      for (long t = 0; t < g.timesteps; ++t) {
        long offset = g.offset_at_timestep(t);
        long width = g.width_at_timestep(t);

        long last_offset = g.offset_at_timestep(t-1);
        long last_width = g.width_at_timestep(t-1);

        long dset = g.dependence_set_at_timestep(t);

        printf("      Timestep %ld (offset %ld, width %ld, last offset %ld, last width %ld):\n",
               t, offset, width, last_offset, last_width);
        printf("        Points:");
        for (long p = offset; p < offset + width; ++p) {
          printf(" %ld", p);
        }
        printf("\n");

        printf("        Dependencies:\n");
        for (long p = offset; p < offset + width; ++p) {
          printf("          Point %ld:", p);
          auto deps = g.dependencies(dset, p);
          for (auto dep : deps) {
            for (long dp = dep.first; dp <= dep.second; ++dp) {
              if (dp >= last_offset && dp < last_offset + last_width) {
                printf(" %ld", dp);
              }
            }
          }
          printf("\n");
        }
        if (verbose > 1) {
          printf("        Reverse Dependencies:\n");
          for (long p = last_offset; p < last_offset + last_width; ++p) {
            printf("          Point %ld:", p);
            auto deps = g.reverse_dependencies(dset, p);
            for (auto dep : deps) {
              for (long dp = dep.first; dp <= dep.second; ++dp) {
                if (dp >= offset && dp < offset + width) {
                  printf(" %ld", dp);
                }
              }
            }
            printf("\n");
          }
        }
      }
    }
  }
}

// IMPORTANT: Keep this up-to-date with kernel implementations
long long count_flops_per_task(const TaskGraph &g, long timestep, long point)
{
  switch(g.kernel.type) {
  case KernelType::EMPTY:
  case KernelType::BUSY_WAIT:
  case KernelType::MEMORY_BOUND:
    return 0;

  case KernelType::COMPUTE_DGEMM:
  {
    long N = sqrt(g.scratch_bytes_per_task / (3 * sizeof(double)));
    return 2 * N * N * N * g.kernel.iterations;
  }

  case KernelType::MEMORY_DAXPY:
    return 0;

  case KernelType::COMPUTE_BOUND:
    return 2 * 64 * g.kernel.iterations + 64;

  case KernelType::COMPUTE_BOUND2:
    return 2 * 32 * g.kernel.iterations;

  case KernelType::IO_BOUND:
    return 0;

  case KernelType::LOAD_IMBALANCE:
  {
    long iterations = select_imbalance_iterations(g.kernel, g.graph_index, timestep, point);
    return 2 * 64 * iterations + 64;
  }

  case KernelType::DIST_IMBALANCE:
  {
    long iterations = select_dist_iterations(g.kernel, g.graph_index, timestep, point);
    return 2 * 64 * iterations + 64;
  }

  case KernelType::COMPUTE_MEMORY:
  {
    return 2 * 64 * g.kernel.iterations * (1-g.kernel.fraction_mem) + 64;
  }

  default:
    assert(false && "unimplemented kernel type");
  };
}

// IMPORTANT: Keep this up-to-date with kernel implementations
long long count_bytes_per_task(const TaskGraph &g, long timestep, long point)
{
  switch(g.kernel.type) {
  case KernelType::EMPTY:
  case KernelType::BUSY_WAIT:
    return 0;

  case KernelType::MEMORY_BOUND:
    return g.scratch_bytes_per_task * g.kernel.iterations / g.kernel.samples;

  case KernelType::MEMORY_DAXPY:
    return g.scratch_bytes_per_task * g.kernel.iterations / g.kernel.samples;

  case KernelType::COMPUTE_DGEMM:
  case KernelType::COMPUTE_BOUND:
  case KernelType::COMPUTE_BOUND2:
  case KernelType::IO_BOUND:
  case KernelType::LOAD_IMBALANCE:
  case KernelType::DIST_IMBALANCE:
    return 0;
  case KernelType::COMPUTE_MEMORY:
    return g.scratch_bytes_per_task * g.kernel.iterations * g.kernel.fraction_mem / g.kernel.samples;
  default:
    assert(false && "unimplemented kernel type");
  };
}

static long long count_flops(const TaskGraph &g)
{
  long long flops = 0;
  for (long t = 0; t < g.timesteps; ++t) {
    long offset = g.offset_at_timestep(t);
    long width = g.width_at_timestep(t);

    for (long point = offset; point < offset + width; ++point) {
      flops += count_flops_per_task(g, t, point);
    }
  }
  return flops;
}

static long long count_bytes(const TaskGraph &g)
{
  long long bytes = 0;
  for (long t = 0; t < g.timesteps; ++t) {
    long offset = g.offset_at_timestep(t);
    long width = g.width_at_timestep(t);

    for (long point = offset; point < offset + width; ++point) {
      bytes += count_bytes_per_task(g, t, point);
    }
  }
  return bytes;
}

static std::tuple<long, long> clamp(long start, long end, long min_value, long max_value) {
  if (end < min_value) {
    return std::tuple<long, long>(min_value, min_value - 1);
  } else if (start > max_value) {
    return std::tuple<long, long>(max_value, max_value - 1);
  } else {
    return std::tuple<long, long>(std::max(start, min_value), std::min(end, max_value));
  }
}

void allocate_bytes(TaskGraph&g){
  std::default_random_engine generator;
  if (g.output_bytes_per_task == 16){
    for (int t = 0; t < g.timesteps; t++){
      long width_t = g.width_at_timestep(t);
      long offset_t = g.offset_at_timestep(t);
      for (int i=0; i<width_t; ++i) {
        g.output_bytes_size[t][i+offset_t] = 16;
      }
    }
  }else{

  for (int t = 0; t < g.timesteps; t++){
    long width_t = g.width_at_timestep(t);
    long offset_t = g.offset_at_timestep(t);
    int nstars=(g.output_bytes_per_task-16)*width_t/16;    // total number output
    int nrolls=10000;  // number of experiment
    int p [width_t] = {};
    float mu,sigma;
    if(g.output_case==0){
      //case 0: uniform case (follow output_bytes_per_task)
        for (int i=0; i<width_t; ++i) {
          g.output_bytes_size[t][i+offset_t] = g.output_bytes_per_task;
        }
    }else if (g.output_case==1){
      //case 1: normal distribution with fixed mu and sigma
      //float mu = onormal_mu;
      //float sigma = onormal_std;
        mu = g.onormal_mu;
        sigma = g.onormal_std;

        std::normal_distribution<double> distribution(mu,sigma);
        //int p [width_t] = {};

        for (int i=0; i<nrolls; ++i) {
          double number = distribution(generator);
          if ((number>=0)&&(number<width_t)) ++p[int(number)];
        }

    }else if (g.output_case==2){
    //case 2: normal distribution with non fixed mu and sigma
        mu = rand() % width_t;
        sigma = rand() % (width_t);
        std::normal_distribution<double> distribution(mu,sigma);
        //int p [width_t] = {};

        for (int i=0; i<nrolls; ++i) {
          double number = distribution(generator);
          if ((number>=0)&&(number<width_t)) ++p[int(number)];
        }
    }else if (g.output_case==3){
          float alpha = 2;
          float beta = 2;

        std::gamma_distribution<double> distribution(alpha,beta);
        //int p [width_t] = {};

        for (int i=0; i<nrolls; ++i) {
          double number = distribution(generator);
          if ((number>=0)&&(number<width_t)) ++p[int(number)];
        }
    }

    if(g.output_case>0){
      int iroll=0;
      for (int i=0; i<width_t; ++i) {
        g.output_bytes_size[t][i+offset_t]=((p[i]*nstars/nrolls)+1)*16;
        iroll = iroll+(g.output_bytes_size[t][i+offset_t]-16)/16;
      }
      for (int i=0; i<width_t; ++i) {
        size_t old_output = g.output_bytes_size[t][i+offset_t];
        g.output_bytes_size[t][i+offset_t]=g.output_bytes_size[t][i+offset_t]+((p[i]*(nstars-iroll)/nrolls))*16;
        iroll = iroll+(g.output_bytes_size[t][i+offset_t]-old_output)/16;
      }
      g.output_bytes_size[t][width_t-1+offset_t]=g.output_bytes_size[t][width_t-1+offset_t]+((nstars-iroll)*16);
    }
  }
}
}

void App::report_timing(double elapsed_seconds) const
{
  long long total_num_tasks = 0;
  long long total_num_deps = 0;
  long long total_local_deps = 0;
  long long total_nonlocal_deps = 0;
  long long flops = 0;
  long long bytes = 0;
  long long local_transfer = 0;
  long long nonlocal_transfer = 0;
  for (auto g : graphs) {
    long long num_tasks = 0;
    long long num_deps = 0;
    long long local_deps = 0;
    long long nonlocal_deps = 0;
#ifdef DEBUG_CORE
    if (enable_graph_validation) {
      assert((has_executed_graph.load() & (1 << g.graph_index)) != 0);
    }
#endif
    for (long t = 0; t < g.timesteps; ++t) {
      long offset = g.offset_at_timestep(t);
      long width = g.width_at_timestep(t);
      long last_offset = g.offset_at_timestep(t-1);
      long last_width = g.width_at_timestep(t-1);
      long dset = g.dependence_set_at_timestep(t);

      num_tasks += width;

      for (long p = offset; p < offset + width; ++p) {
        long point_node = 0;
        long node_first = 0;
        long node_last = -1;
        if (nodes > 0) {
          point_node = p*nodes/g.max_width;
          node_first = point_node * g.max_width / nodes;
          node_last = (point_node + 1) * g.max_width / nodes - 1;
        }

        auto deps = g.dependencies(dset, p);
        for (auto dep : deps) {
          long dep_first, dep_last;
          std::tie(dep_first, dep_last) = clamp(dep.first, dep.second, last_offset, last_offset + last_width - 1);
          num_deps += dep_last - dep_first + 1;
          if (nodes > 0) {
            long initial_first, initial_last, local_first, local_last, final_first, final_last;
            std::tie(initial_first, initial_last) = clamp(dep_first, dep_last, 0, node_first - 1);
            std::tie(local_first, local_last) = clamp(dep_first, dep_last, node_first, node_last);
            std::tie(final_first, final_last) = clamp(dep_first, dep_last, node_last + 1, g.max_width - 1);
            nonlocal_deps += initial_last - initial_first + 1;
            local_deps += local_last - local_first + 1;
            nonlocal_deps += final_last - final_first + 1;
          }
        }
      }
    }

    total_num_tasks += num_tasks;
    total_num_deps += num_deps;
    total_local_deps += local_deps;
    total_nonlocal_deps += nonlocal_deps;
    flops += count_flops(g);
    bytes += count_bytes(g);
    local_transfer += local_deps * g.output_bytes_per_task;
    nonlocal_transfer += nonlocal_deps * g.output_bytes_per_task;
  }

  printf("Total Tasks %lld\n", total_num_tasks);
  printf("Total Dependencies %lld\n", total_num_deps);
  if (nodes > 0) {
    printf("  Local Dependencies %lld (estimated)\n", total_local_deps);
    printf("  Nonlocal Dependencies %lld (estimated)\n", total_nonlocal_deps);
    printf("  Number of Nodes (used for estimate) %ld\n", nodes);
  } else {
    printf("  Unable to estimate local/nonlocal dependencies\n");
  }
  printf("Total FLOPs %lld\n", flops);
  printf("Total Bytes %lld\n", bytes);
  printf("Elapsed Time %e seconds\n", elapsed_seconds);
  printf("FLOP/s %e\n", flops/elapsed_seconds);
  printf("B/s %e\n", bytes/elapsed_seconds);
  printf("Transfer (estimated):\n");
  if (nodes > 0) {
    printf("  Local Bytes %lld\n", local_transfer);
    printf("  Nonlocal Bytes %lld\n", nonlocal_transfer);
    printf("  Local Bandwidth %e B/s\n", local_transfer/elapsed_seconds);
    printf("  Nonlocal Bandwidth %e B/s\n", nonlocal_transfer/elapsed_seconds);
  } else {
    printf("  Unable to estimate local/nonlocal transfer\n");
  }

#ifdef DEBUG_CORE
  printf("Task Graph Execution Mask %llx\n", has_executed_graph.load());
#endif
}
