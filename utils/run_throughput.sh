#!/bin/sh

traffic=uniform
powergate_auto_config=1
off_percentile=50
dim=8

for inj in 0.01 0.05 0.10 0.15 0.20 0.25 0.30 0.35 0.40 0.45 0.5 0.6
do
  for scheme in baseline rpa rpc rflov flov opt_rflov opt_flov
  do
    ../src/booksim ../runfiles/${scheme}/meshcmp_${off_percentile}off.cfg powergate_auto_config=1 traffic=${traffic} powergate_percentile=${off_percentile} k=${dim} injection_rate=${inj} > ../results/throughput/${scheme}/${traffic}_${inj}inj_${dim}dim_${off_percentile}off.log
  done
done
