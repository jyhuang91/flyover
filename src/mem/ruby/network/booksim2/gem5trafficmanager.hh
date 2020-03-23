#ifndef _GEM5_TRAFFICMANAGER_HH_
#define _GEM5_TRAFFICMANAGER_HH_

#include <list>
#include <map>
#include <set>
#include <vector>
#include <cassert>

#include "mem/ruby/network/booksim2/BooksimNetwork.hh"
#include "mem/ruby/network/booksim2/trafficmanager.hh"
#include "mem/ruby/network/booksim2/config_utils.hh"
#include "mem/ruby/network/booksim2/networks/network.hh"
#include "mem/ruby/network/booksim2/flit.hh"
#include "mem/ruby/network/booksim2/buffer_state.hh"
#include "mem/ruby/network/booksim2/stats.hh"
#include "mem/ruby/network/booksim2/traffic.hh"
#include "mem/ruby/network/booksim2/routefunc.hh"
#include "mem/ruby/network/booksim2/outputset.hh"

class MessageBuffer;

class Gem5TrafficManager : public TrafficManager {

protected:
    BooksimNetwork *_net_ptr;
    vector<vector<MessageBuffer *> > _input_buffer;
    vector<vector<MessageBuffer *> > _output_buffer;

    int _vnets;
    vector<int> _last_vnet; // no subnet used in gem5

protected:
    virtual void _RetireFlit(Flit *f, int dest);
    virtual void _GeneratePacket(int source, int stype, int vnet, uint64_t time);

    virtual void _Inject();

public:
    static Gem5TrafficManager *New(Configuration const &config,
            vector<BSNetwork *> const &net, int vnets);

    Gem5TrafficManager(const Configuration &config, const vector<BSNetwork *>
            & net , int vnets);
    virtual ~Gem5TrafficManager();

    virtual void DisplayStats(ostream& out) const;
    virtual void Step();
    void RegisterMessageBuffers(vector<vector<MessageBuffer *> >& in,
                                vector<vector<MessageBuffer *> >& out);

    bool functionalRead(Packet *pkt);
    uint32_t functionalWrite(Packet *pkt);

    void init_net_ptr(BooksimNetwork* net_ptr) { _net_ptr = net_ptr; }
    bool EventsOutstanding();
    bool RouterPowerStateTransition();
    virtual int NextPowerEventCycle() {return 0;}

    //inline
    int getNetworkTime() {
        return _network_time;
    }

protected:
    int _flit_size;
    uint64_t _network_time;
    uint64_t _next_report;
    bool _watch_all_pkts;
};

#endif // #define _GEM5_TRAFFICMANAGER_HH_

