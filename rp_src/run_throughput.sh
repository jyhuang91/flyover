#!/bin/sh

OUT_DIR=../results/0909/13_throughput/rp_conservative
VERSION=rp_conservative
CONFIG=../config/cmp/rp/meshcmp

for injection_rate in 0.01 0.02 0.03 0.04 0.05 0.06 0.07 0.08 0.09 0.1 0.11 0.12 0.13 #0.14 0.15 0.16 0.17 0.18 #0.19 0.2 0.21 0.22 0.23 #0.24 0.25 0.26
do
  ./booksim ${CONFIG}_${injection_rate}.cfg > $OUT_DIR/${VERSION}_$injection_rate 2>&1 &
done
