#!/bin/sh

benchmark=${1}
date=${2}
numthread=${3}
numcpu=${4}
netconfig=${5}
datasize=${6}
system=ALPHA

if [ $numcpu -eq 64 ]; then
    l2size=16MB
elif [ $numcpu -eq 32 ]; then
    l2size=8MB
elif [ $numcpu -eq 16 ]; then
    l2size=4MB
else
    l2size=2MB
fi

if [ $numcpu -eq 64 ]; then
    meshrows=8;
else
    meshrows=4;
fi

./build/${system}/gem5.opt -d m5out/restore/${benchmark}${numthread}_${meshrows}x${meshrows}mesh_sim${datasize} configs/example/fs_booksim2.py --checkpoint-dir=m5out/checkpoints/${benchmark}${numthread}_${numcpu}cpu_sim${datasize}/ --checkpoint-restore=1 --restore-with-cpu=timing --maxinst=1000000000 --ruby --num-cpus=${numcpu} --caches --l2cache --num-dirs=4  --num-l2caches=${numcpu} --l1d_size=32kB --l1i_size=32kB --l2_size=${l2size} --mem-size=1GB --topology=MeshDirCorners --mesh-rows=${meshrows} --booksim-network --booksim-config=configs/ruby_booksim/gem5booksim.cfg > log/$date/${benchmark}${numthread}_${meshrows}x${meshrows}mesh_${datasize} 2>&1 &
