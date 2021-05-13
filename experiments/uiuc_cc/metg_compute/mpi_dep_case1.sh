#!/bin/bash
#SBATCH --partition=cs
#SBATCH --exclusive
#SBATCH --time=03:00:00
#SBATCH --mail-type=ALL

module load openmpi/3.1.1-gcc-7.2.0

VARIANT=forall
echo Variant = $VARIANT

cores=$(( $(echo $SLURM_JOB_CPUS_PER_NODE | cut -d'(' -f 1) / 2 ))

function sweep {
    for s in 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21; do
        for rep in 0 1 2 3 4; do
            if [[ $rep -le $s ]]; then
		srun -n $(( $2 * cores )) -N $2 --ntasks-per-node=$cores --cpus-per-task=2 --cpu_bind cores ../../../mpi/bulk_synchronous -kernel compute_bound -iter $(( 1 << (26-s) )) -type $4 -radix ${RADIX:-5} -steps ${STEPS:-1000} -width $(( $2 * cores )) -output-case 1 -output 64 -output-mean $(( cores)) -output-std $(( cores/$2))
            fi
        done
    done
}

for n in $SLURM_JOB_NUM_NODES; do
    for g in ${NGRAPHS:-1}; do
        for t in ${PATTERN:-stencil_1d}; do
            sweep launch $n $g $t > case1_mpi_${VARIANT}_ngraphs_${g}_type_${t}_nodes_${n}.log
        done
    done
done
