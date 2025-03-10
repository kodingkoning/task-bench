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

#ifndef CORE_C_H
#define CORE_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dependence_type_t {
  TRIVIAL,
  NO_COMM,
  STENCIL_1D,
  STENCIL_1D_PERIODIC,
  DOM,
  TREE,
  FFT,
  ALL_TO_ALL,
  NEAREST,
  SPREAD,
  RANDOM_NEAREST,
  RANDOM_SPREAD,
} dependence_type_t;

typedef enum kernel_type_t {
  EMPTY,
  BUSY_WAIT,
  MEMORY_BOUND,
  COMPUTE_DGEMM,
  MEMORY_DAXPY,
  COMPUTE_BOUND,
  COMPUTE_BOUND2,
  IO_BOUND,
  LOAD_IMBALANCE,
  COMPUTE_MEMORY,
  DIST_IMBALANCE,
} kernel_type_t;

typedef enum dist_type_t {
  UNIFORM,
  NORMAL,
  GAMMA,
  CAUCHY,
  // TODO: add more
} dist_type_t;

typedef enum dist_param_type_t {
  MAX,
  STD,
  BETA,
  ALPHA,
  LAMBDA,
} dist_param_type_t;

typedef struct dist_t {
  dist_type_t type;
  long max; // for the uniform distribution
  long std; // standard deviation for normal
  long a; // for gamma
  double b; // for cauchy
  // TODO: make sure these datayptes are good and add other necessary ones
} dist_t;

typedef struct kernel_t {
  kernel_type_t type;
  long iterations;
  int samples;
  double imbalance; // amount of imbalance as a fraction of the number of iterations
  double fraction_mem; // fraction of iterations that are memory accesses
  dist_t dist;
} kernel_t;

typedef struct interval_t {
  // represents the INCLUSIVE interval from start to end
  long start;
  long end;
} interval_t;

typedef struct interval_list_t {
  void *impl;
} interval_list_t;

void interval_list_destroy(interval_list_t intervals);
long interval_list_num_intervals(interval_list_t intervals);
interval_t interval_list_interval(interval_list_t intervals, long index);

typedef struct task_graph_t {
  long graph_index;
  long timesteps;
  long max_width;
  dependence_type_t dependence;
  long radix; // max number of dependencies in nearest/spread/random patterns
  long period; // period of repetition in spread/random pattern
  double fraction_connected; // fraction of connected nodes in random pattern
  kernel_t kernel;
  size_t output_bytes_per_task;
  size_t scratch_bytes_per_task;
  int nb_fields;
  size_t** output_bytes_size;
  int output_case;
  float onormal_mu;
  float onormal_std;
  float ogamma_alpha;
  float ogamma_beta;
} task_graph_t;

long task_graph_allocate_bytes(task_graph_t graph, int output_case);
long task_graph_offset_at_timestep(task_graph_t graph, long timestep);
long task_graph_width_at_timestep(task_graph_t graph, long timestep);
long task_graph_max_dependence_sets(task_graph_t graph);
long task_graph_timestep_period(task_graph_t graph);
long task_graph_dependence_set_at_timestep(task_graph_t graph, long timestep);
interval_list_t task_graph_reverse_dependencies(task_graph_t graph, long dset, long point);
interval_list_t task_graph_dependencies(task_graph_t graph, long dset, long point);
void task_graph_execute_point_scratch(task_graph_t graph, long timestep, long point,
                                      char *output_ptr, size_t output_bytes,
                                      const char **input_ptr, const size_t *input_bytes,
                                      size_t n_inputs,
                                      char *scratch_ptr, size_t scratch_bytes);
// Hack: This version is here for Spark because allocating scratch_ptr
// through the JVM seems to cause the GC to thrash.
void task_graph_execute_point_scratch_auto(task_graph_t graph, long timestep, long point,
                                           char *output_ptr, size_t output_bytes,
                                           const char **input_ptr, const size_t *input_bytes,
                                           size_t n_inputs,
                                           size_t scratch_bytes);

// FIXME: input_ptr should be const, but this breaks Chapel
void task_graph_execute_point_nonconst(task_graph_t graph, long timestep, long point,
                                       int64_t *output_ptr, size_t output_bytes,
                                       int64_t **input_ptr, const size_t *input_bytes,
                                       size_t n_inputs);
void task_graph_execute_point_scratch_nonconst(task_graph_t graph, long timestep, long point,
                                               int64_t *output_ptr, size_t output_bytes,
                                               int64_t **input_ptr, const size_t *input_bytes,
                                               size_t n_inputs,
                                               char *scratch_ptr, size_t scratch_bytes);
void task_graph_prepare_scratch(char *scratch_ptr, size_t scratch_bytes);

typedef struct task_graph_list_t {
  void *impl;
} task_graph_list_t;

void task_graph_list_destroy(task_graph_list_t graphs);
long task_graph_list_num_task_graphs(task_graph_list_t graphs);
task_graph_t task_graph_list_task_graph(task_graph_list_t graphs, long index);

typedef struct app_t {
  void *impl;
} app_t;

app_t app_create(int argc, char **argv);
void app_destroy(app_t app);
task_graph_list_t app_task_graphs(app_t app);
bool app_verbose(app_t app);
void app_check(app_t app);
void app_display(app_t app);
void app_report_timing(app_t app, double elapsed_seconds);

#ifdef __cplusplus
}
#endif

#endif
