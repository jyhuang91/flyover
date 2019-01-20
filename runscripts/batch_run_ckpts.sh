#!/bin/sh

date=2018-11-04
numthread=32
numcpu=32
datasize=small

for bench in blackscholes bodytrack canneal dedup ferret fluidanimate vips x264
do
  ./runscripts/run_ckpts.sh $bench $date $numthread $numcpu $datasize
done
