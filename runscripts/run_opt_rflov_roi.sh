#!/bin/sh

benchmark=${1}
date=${2}
numthread=${3}
numcpu=${4}
netconfig=${5}
datasize=${6}
system=ALPHA_MESI_Two_Level

if [ $numcpu -eq 64 ]; then
    l2size=16MB
elif [ $numcpu -eq 32 ]; then
    l2size=8MB
elif [ $numcpu -eq 16 ]; then
    l2size=4MB
else
    l2size=2MB
fi

if [ "$netconfig" = "mesh" ]; then
    if [ $numcpu -eq 64 ]; then
        meshrows=8;
    else
        meshrows=4;
    fi
    meshrows=8;
    if [ "$benchmark" = "ssca2" ]; then
        ./build/${system}/gem5.opt -d m5out/restore_opt_rflov/${benchmark}${numthread}_${meshrows}x${meshrows}mesh_p2p-Gnutella31 configs/example/fs_booksim2.py --checkpoint-dir=m5out/checkpoints/${benchmark}${numthread}_${numcpu}cpu_p2p-Gnutella31/ --checkpoint-restore=1 --restore-with-cpu=timing --ruby --num-cpus=${numcpu} --caches --l2cache --num-dirs=4  --num-l2caches=${numcpu} --l1d_size=32kB --l1i_size=32kB --l2_size=${l2size} --l2_assoc=16 --mem-size=1GB --topology=MeshDirCorners --mesh-rows=${meshrows} --booksim-network --noc-pg --booksim-config=configs/ruby_booksim2/gem5booksim_opt_rflov.cfg > log/$date/${benchmark}${numthread}_${meshrows}x${meshrows}mesh_p2p-Gnutella31 2>&1 &
    else
        ./build/${system}/gem5.opt -d m5out/restore_opt_rflov/${benchmark}${numthread}_${meshrows}x${meshrows}mesh_sim${datasize} configs/example/fs_booksim2.py --checkpoint-dir=m5out/checkpoints/${benchmark}${numthread}_${numcpu}cpu_sim${datasize}/ --checkpoint-restore=1 --restore-with-cpu=timing --ruby --num-cpus=${numcpu} --num-dirs=4  --num-l2caches=${numcpu} --l1d_size=32kB --l1i_size=32kB --l2_size=${l2size} --l2_assoc=16 --mem-size=1GB --topology=MeshDirCorners --mesh-rows=${meshrows} --booksim-network --noc-pg --booksim-config=configs/ruby_booksim2/gem5booksim_opt_rflov.cfg > log/$date/${benchmark}${numthread}_${meshrows}x${meshrows}mesh_${datasize} 2>&1 &
    fi
elif [ "$netconfig" = "cmesh" ]; then
    concentration=$(($numthread / 16))
    ./build/${system}/gem5.opt -d m5out/restore_opt_rflov/${benchmark}${numthread}_4x4cmesh_sim${datasize} configs/example/fs_booksim2.py --checkpoint-dir=m5out/checkpoints/${benchmark}${numthread}_${numcpu}cpu_sim${datasize}/ --checkpoint-restore=1 --restore-with-cpu=detailed --ruby --num-cpus=${numthread} --caches --l2cache --num-dirs=4  --num-l2caches=${numthread} --l1d_size=32kB --l1i_size=32kB --l2_size=${l2size} --mem-size=1GB --cmesh=${concentration} --topology=CMeshDirCorners --mesh-rows=4 --booksim-network --noc-pg --booksim-config=configs/ruby_booksim2/gem5booksim_opt_rflov.cfg > log/$date/${benchmark}${numthread}_4x4cmesh_${datasize} 2>&1 &
fi
