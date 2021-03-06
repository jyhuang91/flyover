# FLOV-gem5

This code base includes the integration of booksim2 with gem5, enabling FlyOver
simulations. Most (hopefully all) code changes for power-gating start with
"Power Gate Start" and end with "Power Gate End" comments.

## Getting Simulation System Files and Setup Environment

System files to support gem5 full system simulation are provided in
[gem5_full_system.tar.gz](https://drive.google.com/open?id=158MrScL2ZcWERXCii1d5nWnyJxijWzyL),
download it and do the following steps:

```bash
$ tar xzvf gem5_full_system.tar.gz
$ cd gem5_full_system
$ source setup_gem5_env
```

## Download Checkpoints

For your ease to reproduce the results,
[checkpoints](https://drive.google.com/open?id=1AQKH73O9pKaMwsqRzRPEuL4tQ6KxFyBD)
have been created. Download
[it](https://drive.google.com/open?id=1AQKH73O9pKaMwsqRzRPEuL4tQ6KxFyBD) and
untar it. put it under `gem5/m5out/checkpoints`.

## Run Simulations

bash scripts under `runscripts/` subderectory is provided for simulation runs,
take a look and use the one you want to run (`opt_flov` is for `FLOV+` in the
[TC paper](https://jyhuang91.github.io/papers/tc2020-flyover.pdf)).



## Process Stats and Plot Figures

Python script `process_stats_and_plot.py` can be used to plot the benchmark
result figures.

