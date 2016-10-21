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
    regMsgStats();
    regPerfStats();
    regPowerStats();
}

void
BooksimNetwork::regMsgStats()
{
    _ctrl_flits_received
        .init(m_virtual_networks)
        .name(name() + ".ctrl_flits_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    _ctrl_flits_injected
        .init(m_virtual_networks)
        .name(name() + ".ctrl_flits_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    _data_flits_received
        .init(m_virtual_networks)
        .name(name() + ".data_flits_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    _data_flits_injected
        .init(m_virtual_networks)
        .name(name() + ".data_flits_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    _ctrl_pkts_received
        .init(m_virtual_networks)
        .name(name() + ".ctrl_pkts_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    _ctrl_pkts_injected
        .init(m_virtual_networks)
        .name(name() + ".ctrl_pkts_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    _data_pkts_received
        .init(m_virtual_networks)
        .name(name() + ".data_pkts_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    _data_pkts_injected
        .init(m_virtual_networks)
        .name(name() + ".data_pkts_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;


    for (int i = 0; i < m_virtual_networks; i++) {
        _ctrl_flits_received.subname(i, csprintf("vnet-%i", i));
        _ctrl_flits_injected.subname(i, csprintf("vnet-%i", i));
        _data_flits_received.subname(i, csprintf("vnet-%i", i));
        _data_flits_injected.subname(i, csprintf("vnet-%i", i));
        _ctrl_pkts_received.subname(i, csprintf("vnet-%i", i));
        _ctrl_pkts_injected.subname(i, csprintf("vnet-%i", i));
        _data_pkts_received.subname(i, csprintf("vnet-%i", i));
        _data_pkts_injected.subname(i, csprintf("vnet-%i", i));
    }

    _flits_received
        .name(name() + ".flits_received")
        .flags(Stats::oneline);
    _flits_received = _ctrl_flits_received + _data_flits_received;

    _flits_injected
        .name(name() + ".flits_injected")
        .flags(Stats::oneline);
    _flits_injected = _ctrl_flits_injected + _data_flits_injected;

    _pkts_received
        .name(name() + ".pkts_received")
        .flags(Stats::oneline);
    _pkts_received = _ctrl_pkts_received + _data_pkts_received;

    _pkts_injected
        .name(name() + "pkts_injected")
        .flags(Stats::oneline);
    _pkts_injected = _ctrl_pkts_injected + _data_pkts_injected;

}

void
BooksimNetwork::regPerfStats()
{
    _plat
        .init(m_virtual_networks)
        .name(name() + ".packet_latency")
        .flags(Stats::oneline)
        ;

    _nlat
        .init(m_virtual_networks)
        .name(name() + ".network_latency")
        .flags(Stats::oneline)
        ;

    _qlat
        .init(m_virtual_networks)
        .name(name() + ".queueing_latency")
        .flags(Stats::oneline)
        ;

    _flat
        .init(m_virtual_networks)
        .name(name() + ".flit_latency")
        .flags(Stats::oneline)
        ;

    _frag
        .init(m_virtual_networks)
        .name(name() + ".fragmentation")
        .flags(Stats::oneline)
        ;

    _hops
        .init(m_virtual_networks)
        .name(name() + ".hops")
        .flags(Stats::oneline)
        ;

    _flov_hops
        .init(m_virtual_networks)
        .name(name() + ".flov_hops")
        .flags(Stats::oneline)
        ;

    for (int i = 0; i < m_virtual_networks; i++) {
        _plat.subname(i, csprintf("vnet-%i", i));
        _nlat.subname(i, csprintf("vnet-%i", i));
        _qlat.subname(i, csprintf("vnet-%i", i));
        _flat.subname(i, csprintf("vnet-%i", i));
        _frag.subname(i, csprintf("vnet-%i", i));
        _hops.subname(i, csprintf("vnet-%i", i));
        _flov_hops.subname(i, csprintf("vnet-%i", i));
    }

    _avg_vplat
        .name(name() + ".average_vpkt_latency")
        .flags(Stats::oneline);
    _avg_vplat = _plat / _pkts_received;

    _avg_vnlat
        .name(name() + ".average_vnet_latency")
        .flags(Stats::oneline);
    _avg_vnlat = _nlat / _pkts_received;

    _avg_vqlat
        .name(name() + ".average_vqueue_latency")
        .flags(Stats::oneline);
    _avg_vqlat = _qlat / _pkts_received;

    _avg_vflat
        .name(name() + ".average_vflit_latency")
        .flags(Stats::oneline);
    _avg_vflat = _flat / _flits_received;

    _avg_vfrag
        .name(name() + ".average_vfragmentation")
        .flags(Stats::oneline);
    _avg_vfrag = _frag / _pkts_received;

    _avg_plat.name(name() + ".average_packet_latency");
    _avg_plat = sum(_plat) / sum(_pkts_received);

    _avg_nlat.name(name() + ".average_network_latency");
    _avg_nlat = sum(_nlat) / sum(_pkts_received);

    _avg_qlat.name(name() + ".average_queueing_latency");
    _avg_qlat = sum(_qlat) / sum(_pkts_received);

    _avg_flat.name(name() + ".average_flit_latency");
    _avg_flat = sum(_flat) / sum(_flits_received);

    _avg_frag.name(name() + ".average_fragmentation");
    _avg_frag = sum(_frag) / sum(_pkts_received);

    _avg_vhops
        .name(name() + ".average_vhops")
        .flags(Stats::oneline);
    _avg_vhops = _hops / _pkts_received;

    _avg_hops.name(name() + ".average_hops");
    _avg_hops = sum(_hops) / sum(_pkts_received);

    _avg_vflov_hops
        .name(name() + ".average_vflov_hops")
        .flags(Stats::oneline);
    _avg_vflov_hops = _flov_hops / _pkts_received;

    _avg_flov_hops.name(name() + ".average_flov_hops");
    _avg_flov_hops = sum(_flov_hops) / sum(_pkts_received);
}

void
BooksimNetwork::regPowerStats()
{
    _dynamic_link_power.name(name() + ".link_dynamic_power");
    _static_link_power.name(name() + ".link_static_power");

    _total_link_power.name(name() + ".link_total_power");
    _total_link_power = _dynamic_link_power + _static_link_power;

    _dynamic_router_power.name(name() + ".router_dynamic_power");
    _static_router_power.name(name() + ".router_static_power");
    _clk_power.name(name() + ".clk_power");

    _total_router_power.name(name() + ".router_total_power");
    _total_router_power = _dynamic_router_power +
                          _static_router_power +
                          _clk_power;
}

void
BooksimNetwork::collateStats()
{
    //RubySystem *rs = params()->ruby_system;
    //double time_delta = double(curCycle() - g_ruby_start);
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

bool
BooksimNetwork::isDataMsg(MessageSizeType size_type)
{
    switch(size_type) {
      case MessageSizeType_Control:
      case MessageSizeType_Request_Control:
      case MessageSizeType_Reissue_Control:
      case MessageSizeType_Response_Control:
      case MessageSizeType_Writeback_Control:
      case MessageSizeType_Broadcast_Control:
      case MessageSizeType_Multicast_Control:
      case MessageSizeType_Forwarded_Control:
      case MessageSizeType_Invalidate_Control:
      case MessageSizeType_Unblock_Control:
      case MessageSizeType_Persistent_Control:
      case MessageSizeType_Completion_Control:
        return false;
      case MessageSizeType_Data:
      case MessageSizeType_Response_Data:
      case MessageSizeType_ResponseLocal_Data:
      case MessageSizeType_ResponseL2hit_Data:
      case MessageSizeType_Writeback_Data:
        return true;
      default:
        panic("Invalid range for type MessageSizeType");
        break;
    }
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
