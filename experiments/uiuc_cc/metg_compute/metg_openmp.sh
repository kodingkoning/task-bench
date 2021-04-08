#!/bin/bash
#SBATCH --partition=secondary-Eth
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --mail-type=ALL

export OMP_PROC_BIND=spread

total_cores=$(( $(echo $SLURM_JOB_CPUS_PER_NODE | cut -d'(' -f 1) / 2 ))
cores=$(( $total_cores))

function launch {
    srun -n $1 -N $1 --cpus-per-task=$(( total_cores * 2 )) --cpu_bind none ../../openmp/main "${@:2}" -worker $total_cores
}

function repeat {
    local -n result=$1
    local n=$2
    result=()
    for i in $(seq 1 $n); do
        result+=("${@:3}")
        if (( i < n )); then
            result+=("-and")
        fi
    done
}

function sweep {
    for s in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18; do
        for rep in 0 1 2 3 4; do
            if [[ $rep -le $s ]]; then
                local args
		echo srun -n $2 -N $2 --cpus-per-task=$(( total_cores * 2 )) --cpu_bind none ../../openmp/main -kernel compute_bound -iter $(( 1 << (26-s) )) -type $4 -radix ${RADIX:-5} -steps ${STEPS:-1000} -width $(( $2 * cores )) -field 2 -worker $total_cores
            fi
        done
    done
}

for n in $SLURM_JOB_NUM_NODES; do
    for g in ${NGRAPHS:-1}; do
        for t in ${PATTERN:-stencil_1d}; do
            sweep launch $n $g $t > openmp_ngraphs_${g}_type_${t}_nodes_${n}_test.log
        done
    done
done
