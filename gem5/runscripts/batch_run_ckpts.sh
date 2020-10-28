#!/bin/sh

numthread=32
numcpu=32
datasize=small

for bench in blackscholes bodytrack canneal dedup ferret fluidanimate vips x264
do
  ./runscripts/run_ckpts.sh $bench $numthread $numcpu $datasize
done
