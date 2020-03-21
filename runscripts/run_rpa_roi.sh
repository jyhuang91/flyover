#!/bin/sh

benchmark=${1}
numthread=${2}
numcpu=${3}
datasize=${4}
system=ALPHA_MESI_Two_Level

l2size=256kB
meshrows=8;

logfile=log/rpa/${benchmark}${numthread}_${meshrows}x${meshrows}mesh_${datasize}.log
mkdir -p log/rpa

./build/${system}/gem5.opt \
  -d m5out/restoret_rpa/${benchmark}${numthread}_${meshrows}x${meshrows}mesh_sim${datasize} \
  configs/example/fs_booksim2.py \
  --checkpoint-dir=m5out/checkpoints/${benchmark}${numthread}_${numcpu}cpu_sim${datasize}/ \
  --checkpoint-restore=1 \
  --restore-with-cpu=timing \
  --ruby \
  --num-cpus=${numcpu} \
  --num-dirs=4  \
  --l1d_size=32kB \
  --l1i_size=32kB \
  --l1d_assoc=4 \
  --l1i_assoc=4 \
  --num-l2caches=${numcpu} \
  --l2_size=${l2size} \
  --l2_assoc=8 \
  --mem-size=1GB \
  --topology=MeshDirCorners \
  --mesh-rows=${meshrows} \
  --booksim-network \
  --noc-pg \
  --booksim-config=configs/ruby_booksim2/gem5booksimrpa.cfg \
  > ${logfile} 2>&1 &
