# FLOV-BookSim
A Voting Approach for Adaptive Network-on-Chip Power-Gating

The simulation infrastructure used in FLOV is based on BookSim simulator.

## Compilatioin

You can simply change directory to the `src/` folder and `make` to compile the
code.

```bash
$ cd src
$ make
```

## Run Simulations

To run the simulation, you can execute the compiled binary `./src/booksim
<simulation.cfg>` and give a input configuration file (pick one from `runfiles/`
or write your own).

We also provide some script files to help reproduce the results and plot the
figures. Check out `utils/`.

BookSim Interconnection Network Simulator
=========================================

BookSim is a cycle-accurate interconnection network simulator.
Originally developed for and introduced with the [Principles and Practices of Interconnection Networks](http://cva.stanford.edu/books/ppin/) book, its functionality has since been continuously extended.
The current major release, BookSim 2.0, supports a wide range of topologies such as mesh, torus and flattened butterfly networks, provides diverse routing algorithms and includes numerous options for customizing the network's router microarchitecture.

---

If you use BookSim in your research, we would appreciate the following citation in any publications to which it has contributed:

Nan Jiang, Daniel U. Becker, George Michelogiannakis, James Balfour, Brian Towles, John Kim and William J. Dally. A Detailed and Flexible Cycle-Accurate Network-on-Chip Simulator. In *Proceedings of the 2013 IEEE International Symposium on Performance Analysis of Systems and Software*, 2013.
