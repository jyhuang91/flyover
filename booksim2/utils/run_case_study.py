#!/bin/bash

powergate_auto_config=1
dim=8

for traffic in uniform tornado
do
  for inj in 0.02 0.08
  do
    for scheme in baseline rpa rpc norp nord flov opt_flov
    do
      mkdir -p ../results/latency/${scheme}
      powergate_type=$scheme
      drain_threshold=20
      if [ $scheme = "flov" ] || [ $scheme = "opt_flov" ]; then
        powergate_type="flov"
        drain_threshold=200
      elif [ $scheme = "baseline" ] || [ $scheme = "norp" ]; then
        powergate_type="no_pg"
      fi
      wait_for_tail_credit=1
      if [ $scheme = "rpa" ] || [ $scheme = "rpc" ] || [ $scheme = "norp" ]; then
        wait_for_tail_credit=0
      else
        wait_for_tail_credit=1
      fi
      for off_percent in 10 20 30 40 50 60 70 80
      do
        count=`ps aux | grep booksim | wc -l`
        while [ $count -ge 9 ]; do
          count=`ps aux | grep booksim | wc -l`
          sleep 1
        done
        logfile=../results/latency/${scheme}/${traffic}_${inj}inj_${off_percent}off.log
        ../src/booksim \
          ../runfiles/${scheme}/meshcmp_${off_percent}off.cfg \
          k=${dim} \
          injection_rate=${inj} \
          powergate_type=${powergate_type} \
          converged_threshold=-1 \
          powergate_auto_config=${powergate_auto_config} \
          traffic=${traffic} \
          powergate_percentile=${off_percent} \
          priority=age \
          vc_buf_size=5 \
          packet_size=5 \
          hold_switch_for_packet=1 \
          routing_deadlock_timeout_threshold=512 \
          idle_threshold=20 \
          drain_threshold=${drain_threshold} \
          nord_power_centric_wakeup_threshold=1 \
          wait_for_tail_credit=${wait_for_tail_credit} \
          flov_monitor_epoch=1000 > ${logfile} 2>&1 &
      done
    done
  done
done
