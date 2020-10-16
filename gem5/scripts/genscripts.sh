#!/bin/sh

for bench in blackscholes bodytrack canneal dedup ferret fluidanimate vips x264
do
  ./writescripts.pl $bench 32 --ckpts
done
