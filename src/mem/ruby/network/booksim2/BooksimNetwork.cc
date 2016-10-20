#include <string>
#include <sstream>
#include <fstream>

#include "mem/ruby/system/System.hh"
#include "mem/ruby/network/booksim2/BooksimNetwork.hh"
#include "mem/ruby/network/booksim2/routefunc.hh"
#include "mem/ruby/network/booksim2/networks/network.hh"
#include "mem/ruby/network/booksim2/gem5trafficmanager.hh"
#include "mem/ruby/network/booksim2/booksim_config.hh"

extern TrafficManager * trafficManager;

using namespace std;

BooksimNetwork::BooksimNetwork(const Params *p)
    : Network(p), Consumer(this)
{
    _booksim_config =  new BookSimConfig();
    _booksim_config->ParseFile(p->booksim_config);

    _vc_per_vnet = _booksim_config->GetInt("vc_per_vnet");

    _booksim_config->Assign("nodes", (int) m_nodes);
    _booksim_config->Assign("num_vcs", (int) m_virtual_networks*_vc_per_vnet);
    InitializeRoutingMap(*_booksim_config);
    assert(_booksim_config->GetInt("num_vcs") ==
            m_virtual_networks*_vc_per_vnet);

    _vnet_type_names.resize(m_virtual_networks);

    gPrintActivity = (_booksim_config->GetInt("print_activity") > 0);
    gTrace = (_booksim_config->GetInt("viewer_trace") > 0);

    string watch_out_file = _booksim_config->GetStr("watch_out");
    if (watch_out_file == "") {
        gWatchOut = nullptr;
    } else if (watch_out_file == "-") {
        gWatchOut = &cout;
    } else {
        gWatchOut = new ofstream(watch_out_file.c_str());
    }

    ostringstream node_router_map;
    node_router_map << "{";
    int nodes = p->attached_router_id.size();
    for (int n = 0; n < nodes-1; n++)
        node_router_map << p->attached_router_id[n] << ",";
    node_router_map << p->attached_router_id[nodes-1] << "}";
    _booksim_config->AddStrField("node_router_map", node_router_map.str());

    ostringstream name;
    name << "gem5_booksim_network";
    _net.push_back(nullptr);
    _net[0] = BSNetwork::New(*_booksim_config, name.str());

    _manager = new Gem5TrafficManager( *_booksim_config, _net, m_virtual_networks);
    trafficManager = _manager;

    _next_report_time = 100000;

    _manager->init_net_ptr(this);

}

void
BooksimNetwork::init()
{
    Network::init();
    _manager->RegisterMessageBuffers(m_toNetQueues, m_fromNetQueues);

}

BooksimNetwork::~BooksimNetwork()
{
    // delete manager
    delete _net[0];
    delete _booksim_config;
}

void BooksimNetwork::checkNetworkAllocation(NodeID id, bool ordered,
                                            int network_num,
                                            string vnet_type)
{
    assert(id < m_nodes);
    assert(network_num < m_virtual_networks);

    if (ordered) {
        m_ordered[network_num] = true;
    }
    m_in_use[network_num] = true;

    _vnet_type_names[network_num] = vnet_type;
}

void
BooksimNetwork::setToNetQueue(NodeID id, bool ordered, int network_num,
                                   string vnet_type, MessageBuffer *b)
{
    checkNetworkAllocation(id, ordered, network_num, vnet_type);
    while (m_toNetQueues[id].size() <= network_num) {
        m_toNetQueues[id].push_back(nullptr);
    }
    m_toNetQueues[id][network_num] = b;
    m_toNetQueues[id][network_num]->setConsumer(this);
    m_toNetQueues[id][network_num]->setReceiver(this);
}

void
BooksimNetwork::setFromNetQueue(NodeID id, bool ordered, int network_num,
                                     string vnet_type, MessageBuffer *b)
{
    checkNetworkAllocation(id, ordered, network_num, vnet_type);
    while (m_fromNetQueues[id].size() <= network_num) {
        m_fromNetQueues[id].push_back(nullptr);
    }
    m_fromNetQueues[id][network_num] = b;
    m_fromNetQueues[id][network_num]->setSender(this);
}

void
BooksimNetwork::wakeup()
{
    _manager->_Step();
    if (_manager->in_flight()) {
        scheduleEventAbsolute(clockEdge(Cycles(1)));
    }
}

bool
BooksimNetwork::functionalRead(Packet *pkt)
{
    return _manager->functionalRead(pkt);
}

uint32_t
BooksimNetwork::functionalWrite(Packet *pkt)
{
    return _manager->functionalWrite(pkt);
}

void
BooksimNetwork::regStats()
{
}

void
BooksimNetwork::collateStats()
{
    //RubySystem *rs = params()->ruby_system;
    //double time_delta = double(curCycle() - rs->getStartCycle());
}

void
BooksimNetwork::print(ostream& out) const
{
    out << "[BooksimNetwork]";
}

BooksimNetwork *
BooksimNetworkParams::create()
{
    return new BooksimNetwork(this);
}

// useless stuff
void
BooksimNetwork::makeInLink(NodeID src, SwitchID dest, BasicLink* link,
                           LinkDirection direction,
                           const NetDest& routing_table_entry)
{
}

void
BooksimNetwork::makeOutLink(SwitchID src, NodeID dest, BasicLink* link,
                           LinkDirection direction,
                           const NetDest& routing_table_entry)
{
}

void
BooksimNetwork::makeInternalLink(SwitchID src, SwitchID dest, BasicLink* link,
                           LinkDirection direction,
                           const NetDest& routing_table_entry)
{
}
