/*
 * flovtrafficmanager.hpp
 * - A traffic manager for FlyOver
 *
 * Author: Jiayi Huang
 */

#ifndef _FLOVTRAFFICMANAGER_HPP_
#define _FLOVTRAFFICMANAGER_HPP_

#include <iostream>

#include "config_utils.hpp"
#include "stats.hpp"
#include "trafficmanager.hpp"

class FLOVTrafficManager : public TrafficManager {

private:

  vector<vector<int> > _packet_size;
  vector<vector<int> > _packet_size_rate;
  vector<int> _packet_size_max_val;

  // ============ Statistics ============

  /* ==== Power Gate - Begin ==== */
  vector<Stats *> _flov_hop_stats;
  vector<double> _overall_flov_hop_stats;
  double _plat_high_watermark;
  double _plat_low_watermark;
  int _monitor_counter;
  int _monitor_epoch;
  vector<Stats *> _per_node_plat;
  vector<int> _power_state_votes;
  string _powergate_type;
  /* ==== Power Gate - End ==== */

  /* ==== Power Gate - Begin ==== */
  vector<vector<int> > _idle_cycles;
  vector<int> _router_idle_periods;
  vector<vector<int> > _overall_idle_cycles;
  vector<bool> _wakeup_handshake_latency;
  /* ==== Power Gate - End ==== */
  // ============ Internal methods ============
protected:

  virtual void _RetireFlit( Flit *f, int dest );

  virtual void _Inject();
  virtual void _Step( );

  virtual void _GeneratePacket( int source, int size, int cl, int time );

  virtual void _ClearStats( );

  virtual void _UpdateOverallStats();

public:

  FLOVTrafficManager( const Configuration &config, const vector<Network *> & net );
  virtual ~FLOVTrafficManager( );

  virtual void WriteStats( ostream & os = cout ) const ;
  virtual void DisplayOverallStats( ostream & os = cout ) const ;

};

#endif
