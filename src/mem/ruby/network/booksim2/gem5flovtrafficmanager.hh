#ifndef _GEM5_FLOV_TRAFFICMANAGER_HH_
#define _GEM5_FLOV_TRAFFICMANAGER_HH_

#include <list>
#include <map>
#include <set>
#include <vector>
#include <cassert>

#include "mem/ruby/network/booksim2/BooksimNetwork.hh"
#include "mem/ruby/network/booksim2/gem5trafficmanager.hh"
#include "mem/ruby/network/booksim2/config_utils.hh"
#include "mem/ruby/network/booksim2/networks/network.hh"
#include "mem/ruby/network/booksim2/flit.hh"
#include "mem/ruby/network/booksim2/buffer_state.hh"
#include "mem/ruby/network/booksim2/stats.hh"
#include "mem/ruby/network/booksim2/traffic.hh"
#include "mem/ruby/network/booksim2/routefunc.hh"
#include "mem/ruby/network/booksim2/outputset.hh"

class MessageBuffer;

class Gem5FLOVTrafficManager : public Gem5TrafficManager {

private:
    /* ==== Power Gate - Begin ==== */
    vector<BooksimStats *> _flov_hop_stats;
    vector<double> _overall_flov_hop_stats;
    double _plat_high_watermark;
    double _plat_low_watermark;
    int _monitor_counter;
    int _monitor_epoch;
    vector<BooksimStats *> _per_node_plat;
    vector<int> _power_state_votes;
    string _powergate_type;
    /* ==== Power Gate - End ==== */

protected:
    virtual void _RetireFlit(Flit *f, int dest);
    virtual void _UpdateOverallStats();

public:
    Gem5FLOVTrafficManager(const Configuration &config, const vector<BSNetwork *>
            & net , int vnets);
    ~Gem5FLOVTrafficManager();

    virtual void DisplayOverallStats(ostream& os) const;
    virtual void DumpStats();
    virtual void Step();
    virtual int  NextPowerEventCycle();
};

#endif // #define _GEM5_FLOV_TRAFFICMANAGER_HH_

