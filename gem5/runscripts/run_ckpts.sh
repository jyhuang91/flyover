#!/bin/sh

benchmark=${1}
date=${2}
numthread=${3}
numcpu=${4}
datasize=${5}
system=ALPHA_MESI_Two_Level

mkdir -p log
mkdir -p log/${date}

./build/${system}/gem5.opt -d m5out/checkpoints/${benchmark}${numthread}_${numcpu}cpu_sim${datasize} ./configs/example/fs_booksim2.py --num-cpus=${numcpu} --cpu-type=atomic --mem-size=1GB --checkpoint-dir=m5out/checkpoints/${benchmark}${numthread}_${numcpu}cpu_sim${datasize} --script=./scripts/${benchmark}_${numthread}c_sim${datasize}_ckpts.rcS > log/${date}/${benchmark}${numthread}_${numcpu}_sim${datasize}_ckpts 2>&1 &

