#!/bin/sh

traffic=uniform
powergate_auto_config=1
off_percentile=50

for dim in 6 8 10
do
  #for inj in 0.01 0.05 0.10 0.15 0.20 0.25 0.30 0.35 0.40 0.45 0.5 0.6
  for inj in `seq 0.01 0.01 0.60`
  do
    for scheme in baseline rpa rpc rflov flov opt_rflov opt_flov nord
    do
      powergate_type=$scheme
      if [ $scheme = "rflov" ] || [ $scheme = "opt_rflov" ]; then
        powergate_type="rflov"
      elif [ $scheme = "gflov" ] || [ $scheme = "opt_gflov" ]; then
        powergate_type="flov"
      elif [ $scheme = "baseline" ]; then
        powergate_type="no_pg"
      fi
      count=`ps aux | grep booksim | wc -l`
      while [ $count -eq 26 ]; do
        count=`ps aux | grep booksim | wc -l`
      done
      logfile=../results/throughput/${scheme}/${dim}dim/${traffic}_${inj}inj_${dim}dim_${off_percentile}off.log
      mkdir -p ../results/throughput/${scheme}/${dim}dim
      ../src/booksim \
        ../runfiles/${scheme}/meshcmp_${off_percentile}off.cfg \
        converged_threshold=-1 \
        wait_for_tail_credit=1 \
        powergate_auto_config=1 \
        traffic=${traffic} \
        powergate_percentile=${off_percentile} \
        powergate_type=${powergate_type} \
        vc_buf_size=5 \
        packet_size=5 \
        priority=age \
        hold_switch_for_packet=1 \
        routing_deadlock_timeout_threshold=512 \
        k=${dim} injection_rate=${inj} > ${logfile} &
    done
  done
done
