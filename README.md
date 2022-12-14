# FlyOver

*FlyOver (FLOV)* is a voting approach for adaptive router power-gating in a light-weight
and distributed manner, which includes router microarchitecture enhancement,
adaptive power-gatiing policy, and low-latency dynamic routing algorithms. More
details can be found in our and IEEE TC 2020 paper and IPDPS'17 paper:

Jiayi Huang, Shilpa Bhosekar, Rahul Boyapati, Ningyuan Wang, Byul Hur, Ki Hwan
Yum, and Eun Jung Kim, "[A Voting Approach for Adaptive Network-on-Chip
Power-Gating](https://doi.org/10.1109/TC.2020.3033163)", in *IEEE Transactions
on Computers*, vol. 70, no. 11, pp. 1962-1975, 2021.

Rahul Boyapati\*, Jiayi Huang\*, Ningyuan Wang, Kyung Hoon Kim, Ki Hwan Yum, and
Eun Jung Kim, "[Fly-Over: A Light-Weight Distributed Power-Gating Mechanism for
Energy-Efficient
Networks-on-Chip](https://jyhuang91.github.io/papers/ipdps2017-flyover.pdf)",
in *Proceedings of the IEEE International Parallel and Distributed Processing
Symposium (IPDPS)*, pp. 708-717, 2017.


## About This Repository

This repository contains the implementation of FlyOver, a distributed
network-on-chip power-gating mechanism. BookSim 2.0 is used as the
network-on-chip simulator for both synthetic and full-system simulation.
The booksim2 directory contains the code to run synthetic workloads. The
gem5 directory has the gem5 with BookSim 2.0 integrated for full-system
simulations.


## FlyOver Implementation

The main components of *FLOV* are implemented in the BookSim 2.0 simulator:

- Implementing the voting policy in a new traffic manager class
  `FLOVTrafficManager` inherited from `TrafficManager` to collect votes from
  all the routers and make the transitions of power-gating policy for individual
  routers (among No-FLOV, R-FLOV, and G-FLOV).
- Enhancing the `Network` and `KNCube` classes to support power-gating
  configuration and to add handshake channels for power-gating protocols.
- Introducing `FLOVRouter` router class that inherits from `IQRouter` to
  implement the introduce router microarchitecture changes for flyover
  bypassing, credit relaying of flow control and power-gating handshaking.
- Implementing two dynamic deadlock-free *FLOV* routing algorithms `flov_mesh`
  and `adaptive_flov_mesh` in `routefunc.cpp` to faciliate low-latency packet
  delivery using *FlyOver links* for bypassing in irregular power-gated networks.
- Adding the `DSENT_POWER_Module` class for parameterized power model, whose
  energy and power parameters are obtained from DSENT power model (see
  `utils/dsent_models.cfg`).


## Running Simulations

You can check `booksim2/README.md` and `gem5/README.md` for instructions to run
synthetic and full-system simulations.

---

If you find this useful in your research, please consider citing our work:

    @article{huang:tc:2020:flyover,
     author = {Huang, Jiayi and Bhosekar, Shilpa and Boyapati, Rahul and Wang, Ningyuan and Hur, Byul and Yum, Ki Hwan and Kim, Eun Jung},
     title = {{A Voting Approach for Adaptive Network-on-Chip Power-Gating}},
     jornal = {IEEE Transactions on Computers},
     year = {2021},
     volume = {70},
     number = {11},
     pages = {1962--1975},
     doi = {10.1109/TC.2020.3033163}
    }

    @inproceedings{boyapati:ipdps:2017:flyover,
     author = {Boyapati, Rahul and Huang, Jiayi and Wang, Ningyuan and Kim, Kyung Hoon and Yum, Ki Hwan and Kim, Eun Jung},
     title = {{Fly-Over: A Light-Weight Distributed Power-Gating Mechanism For Energy-Efficient Networks-on-Chip}},
     booktitle = {Proceedings of the 2017 International Parallel and Distributed Processing Symposium (IPDPS)},
     pages = {708--717},
     month = {May},
     year = {2017}
    }

