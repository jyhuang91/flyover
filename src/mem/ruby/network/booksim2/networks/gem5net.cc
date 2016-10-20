/*
 * Network topology for gem5 booksim
 *
 * Author: Jiayi Huang
 * Date: 09/21/16
 */

#include <fstream>
#include <sstream>
#include <limits>
#include <algorithm>

#include "mem/ruby/network/booksim2/networks/gem5net.hh"
#include "mem/ruby/network/booksim2/misc_utils.hh"

vector<int> Gem5Net::node_router_map;
vector<map<int, int> > Gem5Net::node_port_map;

Gem5Net::Gem5Net( const Configuration &config, const string & name) :
    BSNetwork(config, name)
{
    _ComputeSize(config);
    _Alloc();
    _BuildNet(config);
}

void Gem5Net::_ComputeSize(const Configuration &config)
{
    _k = config.GetInt("k");
    _n = config.GetInt("n");

    gK = _k;
    gN = _n;

    node_router_map = config.GetIntArray("node_router_map");
    _size = powi(_k, _n);
    _channels = 2 * _n * _size;
    _nodes = node_router_map.size();
}

void Gem5Net::RegisterRoutingFunctions()
{
//    gRoutingFunctionMap["dor_gem5mesh"] = &dor_gem5mesh;
}

void Gem5Net::_BuildNet(const Configuration &config)
{
    node_port_map.resize(_size);
    for (int n = 0; n < _nodes; n++) {
        int r_id = node_router_map[n];
        int out_port = node_port_map[r_id].size() + 4;
        node_port_map[r_id][n] = out_port;
    }

    int left_router;
    int right_router;

    int right_input;
    int left_input;

    int right_output;
    int left_output;

    ostringstream router_name;

    // latency type, noc or conventional network
    bool use_noc_latency;
    use_noc_latency = (config.GetInt("use_noc_latency") == 1);

    for (int router = 0; router < _size; router++) {

        router_name << "router";

        if (_k > 1) {
            for (int dim_offset = _size / _k; dim_offset >= 1; dim_offset /=
                    _k) {
                router_name << "_" << (router / dim_offset) % _k;
            }
        }

        int radix = 2 * _n + node_port_map[router].size();

        _routers[router] = Router::NewRouter(config, this, router_name.str(),
                router, radix, radix);
        _timed_modules.push_back(_routers[router]);

        router_name.str("");

        for (int dim = 0; dim  < _n; dim++) {

            // find the neighbor
            left_router = _LeftRouter(router, dim);
            right_router = _RightRouter(router, dim);

            // _mesh ? 1 : 2;
            int latency = 1;
            if (!use_noc_latency) latency = 1;

            // get the input channel number
            right_input = _LeftChannel(right_router, dim);
            left_input = _RightChannel(left_router, dim);

            // add the input channel
            _routers[router]->AddInputChannel(_chan[right_input],
                    _chan_cred[right_input]);
            _routers[router]->AddInputChannel(_chan[left_input],
                    _chan_cred[left_input]);

            // set input channel latency
            _chan[right_input]->SetLatency(latency);
            _chan[left_input]->SetLatency(latency);
            _chan_cred[right_input]->SetLatency(latency);
            _chan_cred[left_input]->SetLatency(latency);

            // get the output channel number
            right_output = _RightChannel(router, dim);
            left_output = _LeftChannel(router, dim);

            // add the output channel
            _routers[router]->AddOutputChannel(_chan[right_output],
                    _chan_cred[right_output]);
            _routers[router]->AddOutputChannel(_chan[left_output],
                    _chan_cred[left_output]);

            // set output channel latency
            _chan[right_output]->SetLatency(latency);
            _chan[left_output]->SetLatency(latency);
            _chan_cred[right_output]->SetLatency(latency);
            _chan_cred[left_output]->SetLatency(latency);
        }

        // injection and ejection channel, always 1 latency
        map<int, int>::iterator iter;
        for (iter = node_port_map[router].begin();
                iter != node_port_map[router].end(); iter++) {
            int node = iter->first;
            _routers[router]->AddInputChannel(_inject[node],
                    _inject_cred[node]);
            _routers[router]->AddOutputChannel(_eject[node],
                    _eject_cred[node]);
            _inject[router]->SetLatency(1);
            _eject[router]->SetLatency(1);
        }
    }
}

int Gem5Net::_LeftChannel(int router, int dim)
{
    // The base channel for a router is 2*_n*router
    int base = 2 * _n * router;
    // The offset for a left channel is 2*dim + 1
    int offset = 2 * dim + 1;

    return (base + offset);
}

int Gem5Net::_RightChannel(int router, int dim)
{
    // The base channel for a router is 2*_n*router
    int base = 2 * _n * router;
    // The offset for a left channel is 2*dim + 1
    int offset = 2 * dim;

    return (base + offset);
}

int Gem5Net::_LeftRouter(int router, int dim)
{
    int k_to_dim = powi(_k, dim);
    int loc_in_dim = (router / k_to_dim) % _k;
    int left_router;
    // if at the left edge of the dimension, wraparound
    if (loc_in_dim == 0) {
        left_router = router + (_k-1) * k_to_dim;
    } else {
        left_router = router - k_to_dim;
    }

    return left_router;
}

int Gem5Net::_RightRouter(int router, int dim)
{
    int k_to_dim = powi(_k, dim);
    int loc_in_dim = (router / k_to_dim) % _k;
    int right_router;
    // if at the right edge of the dimension, wraparound
    if (loc_in_dim == (_k - 1)) {
        right_router = router - (_k-1) * k_to_dim;
    } else {
        right_router = router + k_to_dim;
    }

    return right_router;
}

/**********************************************************
 * Routing Helper Functions
 *********************************************************/
int Gem5Net::NodeToRouter(int node)
{
    return node_router_map[node];
}

int Gem5Net::NodeToPort(int node)
{
    int router = node_router_map[node];
    int port = node_port_map[router][node];

    return port;
}

/**********************************************************
 * Routing functions for Gem5Net
 *********************************************************/
//void dor_gem5mesh(const Router *r, const Flit *f, int in_channel, OutputSet
//        *outputs, bool inject)
//{
//    int vcBegin = 0, vcEnd = gNumVCs - 1;
//    if (f->type == Flit::READ_REQUEST) {
//        vcBegin = gReadReqBeginVC;
//        vcEnd = gReadReqEndVC;
//    } else if (f->type == Flit::WRITE_REQUEST) {
//        vcBegin = gWriteReqBeginVC;
//        vcEnd = gWriteReqEndVC;
//    } else if (f->type == Flit::READ_REPLY) {
//        vcBegin = gReadReplyBeginVC;
//        vcEnd = gReadReplyEndVC;
//    } else if (f->type == Flit::WRITE_REPLY) {
//        vcBegin = gWriteReplyBeginVC;
//        vcEnd = gWriteReplyEndVC;
//    }
//    assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc <
//                    0)));
//
//    int out_port;
//
//    if (inject) {
//        out_port = -1;
//    } else {
//        int dest_router = Gem5Net::NodeToRouter(f->dest);
//        if (dest_router == r->GetID()) {
//            out_port = Gem5Net::NodeToPort(f->dest);
//        } else {
//            out_port = dor_next_mesh(r->GetID(), dest_router);
//        }
//    }
//
//    if (!inject && f->watch) {
//        *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
//            << "Adding VC range [" << vcBegin << "," << vcEnd << "]"
//            << " at output port " << out_port
//            << " for flit " << f->id
//            << " (input port " << in_channel
//            << ", destination " << f->dest << ")." << endl;
//    }
//
//    outputs->Clear();
//
//    outputs->AddRange(out_port, vcBegin, vcEnd);
//}

