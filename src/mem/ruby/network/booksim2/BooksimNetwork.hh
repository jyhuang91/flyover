#ifndef __MEM_RUBY_NETWORK_BOOKSIM_NETWORK_HH__
#define __MEM_RUBY_NETWORK_BOOKSIM_NETWORK_HH__

#include <iostream>
#include <vector>

#include "mem/ruby/common/Consumer.hh"
#include "mem/ruby/network/Network.hh"
#include "params/BooksimNetwork.hh"

class Gem5TrafficManager;
class InjectConsumer;
class Configuration;
class BSNetwork;

class BooksimNetwork : public Network, public Consumer
{
public:
    typedef BooksimNetworkParams Params;
    BooksimNetwork(const Params *p);

    ~BooksimNetwork();
    void init();
    void wakeup();

    static bool isDataMsg(MessageSizeType size_type);

    void collateStats();
    void regStats();
    void regMsgStats();
    void regPerfStats();
    void regActivityStats();
    void regPowerStats();
    void print(std::ostream& out) const;

    // set the queue
    void setToNetQueue(NodeID id, bool ordered, int network_num,
                       std::string vnet_type, MessageBuffer *b);
    void setFromNetQueue(NodeID id, bool ordered, int network_num,
                         std::string vnet_type, MessageBuffer *b);

    // Methods used by Topology to setup the network
    void makeOutLink(SwitchID src, NodeID dest, BasicLink * link,
                     LinkDirection direction,
                     const NetDest& routing_table_entry);

    void makeInLink(SwitchID src, NodeID dest, BasicLink * link,
                     LinkDirection direction,
                     const NetDest& routing_table_entry);

    void makeInternalLink(SwitchID src, NodeID dest, BasicLink * link,
                     LinkDirection direction,
                     const NetDest& routing_table_entry);

    //! Function for performing a functional write. The return value
    //! indicates if a message was found that had the required address.
    bool functionalRead(Packet *pkt);

    //! Function for performing a functional write. The return value
    //! indicates the number of messages that were written.
    uint32_t functionalWrite(Packet *pkt);

    std::vector<std::vector<MessageBuffer*> > const & GetInBuffers() {
        return m_toNetQueues;
    }
    std::vector<std::vector<MessageBuffer*> > const & GetOutBuffers() {
        return m_fromNetQueues;
    }

    void increment_received_ctrl_flits(int vnet) {
        _ctrl_flits_received[vnet]++;
    }
    void increment_injected_ctrl_flits(int vnet) {
        _ctrl_flits_injected[vnet]++;
    }
    void increment_received_data_flits(int vnet) {
        _data_flits_received[vnet]++;
    }
    void increment_injected_data_flits(int vnet) {
        _data_flits_injected[vnet]++;
    }
    void increment_received_ctrl_pkts(int vnet) {
        _ctrl_pkts_received[vnet]++;
    }
    void increment_injected_ctrl_pkts(int vnet) {
        _ctrl_pkts_injected[vnet]++;
    }
    void increment_received_data_pkts(int vnet) {
        _data_pkts_received[vnet]++;
    }
    void increment_injected_data_pkts(int vnet) {
        _data_pkts_injected[vnet]++;
    }

    void increment_plat(Cycles lat, int vnet) { _plat[vnet] += lat; }
    void increment_nlat(Cycles lat, int vnet) { _nlat[vnet] += lat; }
    void increment_qlat(Cycles lat, int vnet) { _qlat[vnet] += lat; }
    void increment_flat(Cycles lat, int vnet) { _flat[vnet] += lat; }
    void increment_frag(Cycles lat, int vnet) { _frag[vnet] += lat; }

    void increment_hops(int hops, int vnet) { _hops[vnet] += hops; }
    void increment_flov_hops(int hops, int vnet) { _flov_hops[vnet] += hops; }

private:
    BooksimNetwork(const BooksimNetwork& obj);
    BooksimNetwork& operator=(const BooksimNetwork& obj);

    void checkNetworkAllocation(NodeID id, bool ordered, int network_num,
                                std::string vnet_type);

    std::vector<std::string> _vnet_type_names;
    Gem5TrafficManager* _manager;
    Configuration* _booksim_config;
    std::vector<BSNetwork*> _net;
    int _next_report_time;
    int _vcs_per_vnet;
    int _flit_size;	// in Byte
    uint32_t _buffers_per_vc;

    // Statistical variables for performance
    Stats::Vector _ctrl_flits_received;
    Stats::Vector _ctrl_flits_injected;
    Stats::Vector _data_flits_received;
    Stats::Vector _data_flits_injected;
    Stats::Formula _flits_received;
    Stats::Formula _flits_injected;
    Stats::Vector _ctrl_pkts_received;
    Stats::Vector _ctrl_pkts_injected;
    Stats::Vector _data_pkts_received;
    Stats::Vector _data_pkts_injected;
    Stats::Formula _pkts_received;
    Stats::Formula _pkts_injected;

    // control and data packets are split into vnets,
    // can see from ctrl and data inject/receive stats
    Stats::Vector _plat;
    Stats::Vector _nlat;
    Stats::Vector _qlat;
    Stats::Vector _flat;
    Stats::Vector _frag;

    Stats::Formula _avg_vplat;
    Stats::Formula _avg_vnlat;
    Stats::Formula _avg_vqlat;
    Stats::Formula _avg_vflat;
    Stats::Formula _avg_vfrag;
    Stats::Formula _avg_plat;
    Stats::Formula _avg_nlat;
    Stats::Formula _avg_qlat;
    Stats::Formula _avg_flat;
    Stats::Formula _avg_frag;

    Stats::Vector _hops;
    Stats::Formula _avg_vhops;
    Stats::Formula _avg_hops;

    Stats::Vector _flov_hops;
    Stats::Formula _avg_vflov_hops;
    Stats::Formula _avg_flov_hops;

    // Statistical variables for activities
    Stats::Vector _router_buffer_reads;
    Stats::Vector _router_buffer_writes;
    Stats::Vector _inject_link_activity;
    Stats::Vector _eject_link_activity;
    Stats::Vector _int_link_activity;
//    Stats::Formula _buffer_reads;
//    Stats::Formula _buffer_writes;
//    Stats::Formula _link_activity;

    // Statistical variables for power
    Stats::Scalar _dynamic_link_power;
    Stats::Scalar _static_link_power;
    Stats::Formula _total_link_power;

    Stats::Scalar _dynamic_router_power;
    Stats::Scalar _static_router_power;
    Stats::Scalar _clk_power;
    Stats::Formula _total_router_power;

};

//extern inline
inline std::ostream&
operator<<(std::ostream& out, const BooksimNetwork& obj)
{
    obj.print(out);
    out << std::flush;
    return out;
}

#endif // __MEM_RUBY_NETWORK_BOOKSIM_NETWORK_HH__

