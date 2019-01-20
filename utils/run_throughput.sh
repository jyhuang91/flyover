#!/bin/sh

traffic=uniform
powergate_auto_config=1
off_percentile=50

for dim in 6 8 10
do
  #for inj in 0.01 0.05 0.10 0.15 0.20 0.25 0.30 0.35 0.40 0.45 0.5 0.6
  for inj in `seq 0.01 0.01 0.60`
  do
    for scheme in baseline rpa rpc rflov flov opt_rflov opt_flov
    do
      powergate_type=$scheme
      if [ $scheme = "rflov" ] || [ $scheme = "opt_rflov" ]; then
        powergate_type="rflov"
      elif [ $scheme = "flov" ] || [ $scheme = "opt_flov" ]; then
        powergate_type="flov"
      elif [ $scheme = "baseline" ]; then
        powergate_type="no_pg"
      fi
      count=`ps aux | grep booksim | wc -l`
      while [ $count -eq 5 ]; do
        count=`ps aux | grep booksim | wc -l`
      done
      mkdir -p ../results/throughput/${scheme}/${dim}dim
      ../src/booksim ../runfiles/${scheme}/meshcmp_${off_percentile}off.cfg powergate_auto_config=1 traffic=${traffic} powergate_percentile=${off_percentile} powergate_type=${powergate_type} k=${dim} injection_rate=${inj} > ../results/throughput/${scheme}/${dim}dim/${traffic}_${inj}inj_${dim}dim_${off_percentile}off.log &
    done
  done
done
