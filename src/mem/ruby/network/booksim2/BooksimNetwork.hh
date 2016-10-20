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

    void collateStats();
    void regStats();
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
        return &m_fromNetQueues;
    }

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
    int _vc_per_vnet;

    // Statistical variables
    Stats::Vector _flits_received;
    Stats::Vector _flits_injected;
    Stats::Vector _ctrl_pkts_received;
    Stats::Vector _ctrl_pkts_injected;
    Stats::Vector _data_pkts_received;
    Stats::Vector _data_pkts_injected;

    Stats::Vector _ctrl_flit_nlat;  // network latency
    Stats::Vector _ctrl_flit_qlat;  // queuing latency
    Stats::Vector _ctrl_flit_lat;
    Stats::Vector _data_flit_netlat;
    Stats::Vector _data_flit_qlat;
    Stats::Vector _data_flit_lat;
    Stats::Vector _ctrl_pkt_netowrk_latency;
    Stats::Vector _ctrl_pkt_queueing_latency;
    Stats::Vector _data_pkt_network_latency;
    Stats::Vector _data_pkt_queueing_latency;
    Stats::Vector _pkt_network_latency;
    Stats::Vector _pkt_queueing_latency;

    Stats::Formula _avg_ctrl_flit_network_latency;
    Stats::Formula _avg_ctrl_flit_queueing_latency;
    Stats::Formula _avg_ctrl_flit_latency;
    Stats::Formula _avg_data_flit_network_latency;
    Stats::Formula _avg_data_flit_queueing_latency;
    Stats::Formula _avg_data_flit_latency;
    Stats::Formula _avg_ctrl_pkt_network_latency;
    Stats::Formula _avg_ctrl_pkt_queueing_latency;
    Stats::Formula _avg_ctrl_pkt_latency;
    Stats::Formula _avg_data_pkt_network_latency;
    Stats::Formula _avg_data_pkt_queueing_latency;
    Stats::Formula _avg_data_pkt_latency;
    Stats::Formula _avg_pkt_network_latency;
    Stats::Formula _avg_pkt_queueing_latency;
    Stats::Formula _avg_pkt_latency;
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

