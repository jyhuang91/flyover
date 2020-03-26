/*
 * Network topology for gem5 booksim
 *
 * Author: Jiayi Huang
 * Date: 09/21/16
 */

#ifndef _GEM5NET_HH_
#define _GEM5NET_HH_

#include <cassert>
#include <string>
#include <map>
#include <list>
#include <vector>

#include "mem/ruby/network/booksim2/networks/network.hh"
#include "mem/ruby/network/booksim2/routefunc.hh"

class Gem5Net: public BSNetwork {

    int _k;
    int _n;

    /* association between nodes and routers */
    static vector<int> node_router_map;
    /* association between destination nodes and ejection port,
     * node_port_map[router_id][destination_id] = ejction_port_id */
    static vector<map<int, int> > node_port_map;

    void _ComputeSize(const Configuration &config);
    void _BuildNet(const Configuration &config);

    int _LeftChannel(int router, int dim);
    int _RightChannel(int router, int dim);

    int _LeftRouter(int router, int dim);
    int _RightRouter(int router, int dim);

public:

    Gem5Net(const Configuration &config, const string &name);
    //~Gem5Net();

    static void RegisterRoutingFunctions();

    static int NodeToRouter(int node);
    static int NodeToPort(int node);

    int GetN() const { return _n; }
    int GetK() const { return _k; }

    virtual void InsertRandomFaults(const Configuration &config) {}
};

//void dor_gem5mesh(const BSRouter *r, const Flit *f, int in_channel, OutputSet
//        *outputs, int inject);

#endif
