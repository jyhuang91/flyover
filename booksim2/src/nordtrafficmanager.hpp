/*
 * nordtrafficmanager.hpp
 * - A trafficmanager for NoRD
 *
 * Author: Jiayi Huang
 */

#ifndef _NORDTRAFFICMANAGER_HPP_
#define _NORDTRAFFICMANAGER_HPP_

#include <cassert>
#include <unordered_map>

#include "trafficmanager.hpp"
#include "buffer.hpp"

class NoRDTrafficManager : public TrafficManager {

private:

  vector<vector<int> > _packet_size;
  vector<vector<int> > _packet_size_rate;
  vector<int> _packet_size_max_val;

  vector<vector<bool> > _during_bypassing;

  int _routing_deadlock_timeout_threshold;
  int _performance_centric_wakeup_threshold;
  int _power_centric_wakeup_threshold;
  int _wakeup_monitor_epoch;
  vector<int> _wakeup_monitor_vc_requests;
  vector<bool> _performance_centric_routers;
  set<int> _routers_to_watch_power_gating;
  // ============ Internal methods ============
protected:

  vector<vector<Buffer *> > _buffers;

  virtual void _Inject();
  virtual void _Step( );

  virtual void _GeneratePacket( int source, int size, int cl, int time );

public:

  NoRDTrafficManager( const Configuration &config, const vector<Network *> & net );
  virtual ~NoRDTrafficManager( );

};

#endif
