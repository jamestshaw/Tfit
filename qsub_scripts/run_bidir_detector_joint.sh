#PBS -S /bin/bash

#PBS -N Danko2013_merged
#PBS -m ae
#PBS -M joseph.azofeifa@colorado.edu

#PBS -e /Users/azofeifa/qsub_errors/EMG/
#PBS -o /Users/azofeifa/qsub_stdo/EMG/

#PBS -l walltime=12:00:00
#PBS -l nodes=1:ppn=64
#PBS -l mem=8gb

hostlist=$( cat $PBS_NODEFILE | sort | uniq | tr '\n' ',' | sed -e 's/,$//' )
# -- OpenMP environment variables --
OMP_NUM_THREADS=64
export OMP_NUM_THREADS
module load gcc_4.9.2
module load mpich_3.1.4

#================================================================
#paths to config and src
src=/Users/azofeifa/Lab/Tfit/src/Tfit
config_file=/Users/azofeifa/Lab/Tfit/config_files/config_file.txt


#================================================================
#calling command
cmd="mpirun -np $PBS_NUM_NODES -hosts ${hostlist}"
$cmd $src bidir -config $config_file -ij $bg_file  -tss $TSS  -o $out_directory -log_out $tmp_log_directory -N $name
#================================================================