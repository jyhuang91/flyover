// $Id$

/*
 Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

//  void _ComputeStats( const vector<int> & stats, int *sum, int *min = NULL, int *max = NULL, int *min_pos = NULL, int *max_pos = NULL ) const;

//  virtual bool _SingleSim( );

//  void _DisplayRemaining( ostream & os = cout ) const;

//  void _LoadWatchList(const string & filename);

  virtual void _UpdateOverallStats();

//  virtual string _OverallStatsCSV(int c = 0) const;

//  int _GetNextPacketSize(int cl) const;
//  double _GetAveragePacketSize(int cl) const;

public:

  FLOVTrafficManager( const Configuration &config, const vector<Network *> & net );
  virtual ~FLOVTrafficManager( );

//  bool Run( );

  virtual void WriteStats( ostream & os = cout ) const ;
//  virtual void UpdateStats( ) ;
//  virtual void DisplayStats( ostream & os = cout ) const ;
  virtual void DisplayOverallStats( ostream & os = cout ) const ;
//  virtual void DisplayOverallStatsCSV( ostream & os = cout ) const ;

};

#endif
