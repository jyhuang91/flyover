/*
 * gem5nordtrafficmanager.hh
 * - A traffic manager for NoRD gem5 simulation
 *
 * Author: Jiayi Huang
 */

#ifndef _GEM5_NORD_TRAFFICMANAGER_HH_
#define _GEM5_NORD_TRAFFICMANAGER_HH_

#include <cassert>
#include <unordered_map>

#include "mem/ruby/network/booksim2/gem5trafficmanager.hh"
#include "mem/ruby/network/booksim2/buffer.hh"

class Gem5NoRDTrafficManager : public Gem5TrafficManager {

private:
    /* ==== Power Gate - Begin ==== */
    string _powergate_type;
    int _routing_deadlock_timeout_threshold;
    int _performance_centric_wakeup_threshold;
    int _power_centric_wakeup_threshold;
    int _wakeup_monitor_epoch;
    int _last_monitor_epoch;
    vector<int> _wakeup_monitor_vc_requests;
    vector<bool> _performance_centric_routers;
    set<int> _routers_to_watch_power_gating;
    vector<bool> _bypass_nodes;
    vector<bool> _bypass_routers;
    vector<vector<Buffer *> > _buffers;
    /* ==== Power Gate - End ==== */

protected:
    virtual void _GeneratePacket(int source, int stype, int vnet, uint64_t time);

public:
    Gem5NoRDTrafficManager(const Configuration &config, const vector<BSNetwork *>
            & net , int vnets);
    ~Gem5NoRDTrafficManager();

    virtual void Step();
    virtual int  NextPowerEventCycle();
};

#endif // #define _GEM5_NORD_TRAFFICMANAGER_HH_

