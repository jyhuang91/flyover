#!/bin/sh

datasize=small

for bench in blackscholes bodytrack canneal dedup ferret fluidanimate vips x264
do
    ./runscripts/run_baseline_roi.sh $bench 32 32 $datasize
    ./runscripts/run_rpa_roi.sh $bench 32 32 $datasize
    ./runscripts/run_rpc_roi.sh $bench 32 32 $datasize
    ./runscripts/run_nord_roi.sh $bench 32 32 $datasize
    ./runscripts/run_flov_roi.sh $bench 32 32 $datasize
    ./runscripts/run_opt_flov_roi.sh $bench 32 32 $datasize
done

