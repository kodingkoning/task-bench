/*
 * Copyright (c) 2009-2018 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 *
 */
#ifndef _TESTSCOMMON_H
#define _TESTSCOMMON_H

/* parsec things */
#include "parsec.h"

/* system and io */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Plasma and math libs */
#include <math.h>
#include <cblas.h>
#include <lapacke.h>
#include <core_blas.h>

#include "parsec/profiling.h"
#include "parsec/parsec_internal.h"
#include "parsec/utils/debug.h"
#include "dplasma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* these are globals in common.c */
extern const char *PARSEC_SCHED_NAME[];
extern int unix_timestamp;
extern char cwd[];

/* Update PASTE_CODE_PROGRESS_KERNEL below if you change this list */
enum iparam_t {
  IPARAM_RANK,         /* Rank                              */
  IPARAM_NNODES,       /* Number of nodes                   */
  IPARAM_NCORES,       /* Number of cores                   */
  IPARAM_NGPUS,        /* Number of GPUs                    */
  IPARAM_P,            /* Rows in the process grid          */
  IPARAM_Q,            /* Columns in the process grid       */
  IPARAM_M,            /* Number of rows of the matrix      */
  IPARAM_N,            /* Number of columns of the matrix   */
  IPARAM_K,            /* RHS or K                          */
  IPARAM_LDA,          /* Leading dimension of A            */
  IPARAM_LDB,          /* Leading dimension of B            */
  IPARAM_LDC,          /* Leading dimension of C            */
  IPARAM_IB,           /* Inner-blocking size               */
  IPARAM_NB,           /* Number of columns in a tile       */
  IPARAM_MB,           /* Number of rows in a tile          */
  IPARAM_SNB,          /* Number of columns in a super-tile */
  IPARAM_SMB,          /* Number of rows in a super-tile    */
  IPARAM_HMB,          /* Small MB for recursive hdags */
  IPARAM_HNB,          /* Small NB for recursive hdags */
  IPARAM_CHECK,        /* Checking activated or not         */
  IPARAM_VERBOSE,      /* How much noise do we want?        */
  IPARAM_SCHEDULER,    /* User-selected scheduler */
  IPARAM_SIZEOF
};

#define PARSEC_SCHEDULER_DEFAULT 0
#define PARSEC_SCHEDULER_LFQ 1
#define PARSEC_SCHEDULER_LTQ 2
#define PARSEC_SCHEDULER_AP  3
#define PARSEC_SCHEDULER_LHQ 4
#define PARSEC_SCHEDULER_GD  5
#define PARSEC_SCHEDULER_PBQ 6
#define PARSEC_SCHEDULER_IP  7
#define PARSEC_SCHEDULER_RND 8

void iparam_default_gemm(int* iparam);
void iparam_default_ibnbmb(int* iparam, int ib, int nb, int mb);

#define PASTE_CODE_IPARAM_LOCALS(iparam)                                \
    rank  = iparam[IPARAM_RANK];                                    \
    nodes = iparam[IPARAM_NNODES];                                  \
    cores = iparam[IPARAM_NCORES];                                  \
    gpus  = iparam[IPARAM_NGPUS];                                   \
    P     = iparam[IPARAM_P];                                       \
    Q     = iparam[IPARAM_Q];                                       \
    M     = iparam[IPARAM_M];                                       \
    N     = iparam[IPARAM_N];                                       \
    K     = iparam[IPARAM_K];                                       \
    NRHS  = K;                                                      \
    LDA   = max(M, iparam[IPARAM_LDA]);                             \
    LDB   = max(N, iparam[IPARAM_LDB]);                             \
    LDC   = max(K, iparam[IPARAM_LDC]);                             \
    IB    = iparam[IPARAM_IB];                                      \
    MB    = iparam[IPARAM_MB];                                      \
    NB    = iparam[IPARAM_NB];                                      \
    SMB   = iparam[IPARAM_SMB];                                     \
    SNB   = iparam[IPARAM_SNB];                                     \
    HMB   = iparam[IPARAM_HMB];                                     \
    HNB   = iparam[IPARAM_HNB];                                     \
    MT    = (M%MB==0) ? (M/MB) : (M/MB+1);                          \
    NT    = (N%NB==0) ? (N/NB) : (N/NB+1);                          \
    KT    = (K%MB==0) ? (K/MB) : (K/MB+1);                          \
    check = iparam[IPARAM_CHECK];                                   \
    loud  = iparam[IPARAM_VERBOSE];                                 \
    scheduler = iparam[IPARAM_SCHEDULER];                           \
    (void)rank;(void)nodes;(void)cores;(void)gpus;(void)P;(void)Q;(void)M;(void)N;(void)K;(void)NRHS; \
    (void)LDA;(void)LDB;(void)LDC;(void)IB;(void)MB;(void)NB;(void)MT;(void)NT;(void)KT; \
    (void)SMB;(void)SNB;(void)HMB;(void)HNB;(void)check;(void)loud; \
    (void)scheduler;

/*******************************
 * globals values
 *******************************/

#if defined(PARSEC_HAVE_MPI)
extern MPI_Datatype SYNCHRO;
#endif  /* PARSEC_HAVE_MPI */

void print_usage(void);

parsec_context_t *setup_parsec(int argc, char* argv[], int *iparam);
void cleanup_parsec(parsec_context_t* parsec, int *iparam);

/**
 * No macro with the name max or min is acceptable as there is
 * no way to correctly define them without borderline effects.
 */
#undef max
#undef min
static inline int max(int a, int b) { return a > b ? a : b; }
static inline int min(int a, int b) { return a < b ? a : b; }


/* Paste code to allocate a matrix in desc if cond_init is true */
#define PASTE_CODE_ALLOCATE_MATRIX(DC, COND, TYPE, INIT_PARAMS)      \
    if(COND) {                                                          \
        DC.mat = parsec_data_allocate((size_t)DC.super.nb_local_tiles * \
                                        (size_t)DC.super.bsiz *      \
                                        (size_t)parsec_datadist_getsizeoftype(DC.super.mtype)); \
        parsec_data_collection_set_key((parsec_data_collection_t*)&DC, #DC);          \
    }


#ifdef __cplusplus
}
#endif

#endif /* _TESTSCOMMON_H */
