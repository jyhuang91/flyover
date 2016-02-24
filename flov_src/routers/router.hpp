// $Id: router.hpp 5188 2012-08-30 00:31:31Z dub $

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
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

#ifndef _ROUTER_HPP_
#define _ROUTER_HPP_

#include <string>
#include <vector>
#include <deque>  // Jiayi

#include "timed_module.hpp"
#include "flit.hpp"
#include "credit.hpp"
#include "flitchannel.hpp"
#include "channel.hpp"
#include "config_utils.hpp"
#include "handshake.hpp"	// Jiayi

#define HANDSHAKE

typedef Channel<Credit> CreditChannel;
#ifdef HANDSHAKE
typedef Channel<Handshake> HandshakeChannel;
#endif

class Router : public TimedModule {

// Jiayi
public:
  enum ePowerState { state_min = 0, power_off = state_min, 
          power_on, draining, wakeup, state_max = wakeup };
  static const char * const POWERSTATE[];

protected:

  static int const STALL_BUFFER_BUSY;
  static int const STALL_BUFFER_CONFLICT;
  static int const STALL_BUFFER_FULL;
  static int const STALL_BUFFER_RESERVED;
  static int const STALL_CROSSBAR_CONFLICT;

  int _id;
  
  int _inputs;
  int _outputs;
  
  int _classes;

  int _input_speedup;
  int _output_speedup;
  
  double _internal_speedup;
  double _partial_internal_cycles;

  int _crossbar_delay;
  int _credit_delay;
  
  vector<FlitChannel *>   _input_channels;
  vector<CreditChannel *> _input_credits;
  vector<FlitChannel *>   _output_channels;
  vector<CreditChannel *> _output_credits;
  vector<bool>            _channel_faults;
#ifdef HANDSHAKE	// Jiayi
  vector<HandshakeChannel *> _input_handshakes;
  vector<HandshakeChannel *> _output_handshakes;
#endif

#ifdef TRACK_FLOWS
  vector<vector<int> > _received_flits;
  vector<vector<int> > _stored_flits;
  vector<vector<int> > _sent_flits;
  vector<vector<int> > _outstanding_credits;
  vector<vector<int> > _active_packets;
#endif

#ifdef TRACK_STALLS
  vector<int> _buffer_busy_stalls;
  vector<int> _buffer_conflict_stalls;
  vector<int> _buffer_full_stalls;
  vector<int> _buffer_reserved_stalls;
  vector<int> _crossbar_conflict_stalls;
#endif

  virtual void _InternalStep() = 0;

  // Jiayi, router status (ON or OFF) for power gating
  ePowerState _power_state;  
  double _power_off_cycles; // number of poweroff cycles for current kernel
  double _total_power_off_cycles; // total number of poweroff cycles (accumulated);
  double _total_run_time; // in cycles	
  int _idle_threshold;
  int _drain_threshold;
  int _bet_threshold;
  int _wakeup_threshold;
  int _idle_timer;
  int _drain_timer;
  int _off_timer;  // consecutive off cycles
  int _wakeup_timer;
  bool _wakeup_signal;
  double _off_counter;
  double _drain_counter;
  double _drain_timeout_counter;
  deque<int> _drain_time_q;
  int _max_drain_time;
  int _min_drain_time;
  vector<ePowerState> _neighbor_state;
  vector<ePowerState> _downstream_state;
  double _outstanding_requests;
  vector<bool> _drain_done_sent;
  vector<bool> _drain_tags;
  bool _router_state; // set by trafficmanager, on is true, off is false
	
public:
 Router( const Configuration& config,
	  Module *parent, const string & name, int id,
	  int inputs, int outputs );

  static Router *NewRouter( const Configuration& config,
			    Module *parent, const string & name, int id,
			    int inputs, int outputs );

  virtual void AddInputChannel( FlitChannel *channel, CreditChannel *backchannel );
  virtual void AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel );
#ifdef HANDSHAKE	// Jiayi
  virtual void AddInputHandshake( HandshakeChannel *channel );
  virtual void AddOutputHandshake( HandshakeChannel *channel );
#endif

  inline FlitChannel * GetInputChannel( int input ) const {
    assert((input >= 0) && (input < _inputs));
    return _input_channels[input];
  }
  inline FlitChannel * GetOutputChannel( int output ) const {
    assert((output >= 0) && (output < _outputs));
    return _output_channels[output];
  }

  virtual void ReadInputs( ) = 0;
  virtual void PowerStateEvaluate( ) = 0; // Jiayi
  virtual void Evaluate( );
  virtual void WriteOutputs( ) = 0;

  void OutChannelFault( int c, bool fault = true );
  bool IsFaultyOutput( int c ) const;

  inline int GetID( ) const {return _id;}


  virtual int GetUsedCredit(int o) const = 0;
  virtual int GetBufferOccupancy(int i) const = 0;

#ifdef TRACK_BUFFERS
  virtual int GetUsedCreditForClass(int output, int cl) const = 0;
  virtual int GetBufferOccupancyForClass(int input, int cl) const = 0;
#endif

#ifdef TRACK_FLOWS
  inline vector<int> const & GetReceivedFlits(int c) const {
    assert((c >= 0) && (c < _classes));
    return _received_flits[c];
  }
  inline vector<int> const & GetStoredFlits(int c) const {
    assert((c >= 0) && (c < _classes));
    return _stored_flits[c];
  }
  inline vector<int> const & GetSentFlits(int c) const {
    assert((c >= 0) && (c < _classes));
    return _sent_flits[c];
  }
  inline vector<int> const & GetOutstandingCredits(int c) const {
    assert((c >= 0) && (c < _classes));
    return _outstanding_credits[c];
  }

  inline vector<int> const & GetActivePackets(int c) const {
    assert((c >= 0) && (c < _classes));
    return _active_packets[c];
  }

  inline void ResetFlowStats(int c) {
    assert((c >= 0) && (c < _classes));
    _received_flits[c].assign(_received_flits[c].size(), 0);
    _sent_flits[c].assign(_sent_flits[c].size(), 0);
  }
#endif

  virtual vector<int> UsedCredits() const = 0;
  virtual vector<int> FreeCredits() const = 0;
  virtual vector<int> MaxCredits() const = 0;

#ifdef TRACK_STALLS
  inline int GetBufferBusyStalls(int c) const {
    assert((c >= 0) && (c < _classes));
    return _buffer_busy_stalls[c];
  }
  inline int GetBufferConflictStalls(int c) const {
    assert((c >= 0) && (c < _classes));
    return _buffer_conflict_stalls[c];
  }
  inline int GetBufferFullStalls(int c) const {
    assert((c >= 0) && (c < _classes));
    return _buffer_full_stalls[c];
  }
  inline int GetBufferReservedStalls(int c) const {
    assert((c >= 0) && (c < _classes));
    return _buffer_reserved_stalls[c];
  }
  inline int GetCrossbarConflictStalls(int c) const {
    assert((c >= 0) && (c < _classes));
    return _crossbar_conflict_stalls[c];
  }

  inline void ResetStallStats(int c) {
    assert((c >= 0) && (c < _classes));
    _buffer_busy_stalls[c] = 0;
    _buffer_conflict_stalls[c] = 0;
    _buffer_full_stalls[c] = 0;
    _buffer_reserved_stalls[c] = 0;
    _crossbar_conflict_stalls[c] = 0;
  }
#endif

  inline int NumInputs() const {return _inputs;}
  inline int NumOutputs() const {return _outputs;}

  // *************************************************************************
  // Jiayi, Power On or Off the router
  inline void PowerOn() {_power_state = power_on;}
  inline void PowerOff() {_power_state = power_off;}
  inline void SetPowerState( ePowerState s ) {_power_state = s;}
  inline Router::ePowerState GetPowerState() const {return _power_state;}
  inline double GetPowerOffCycles() const {return _power_off_cycles;}
  inline void ResetPowerOffCycles() {_power_off_cycles = 0;} // _power_off_cycles is reset when each kernel is finished
  inline double GetTotalPowerOffCycles() const {return _total_power_off_cycles;}
	inline double GetTotalRunTime() const {return _total_run_time;}
  inline void AddRunTime(double kernel_run_time) {_total_run_time += kernel_run_time;}
  inline double GetDrainCounter() const {return _drain_counter;}
  inline double GetDrainTimeoutCounter() const {return _drain_timeout_counter;}
  inline int GetMaxDrainTime() const {return _max_drain_time;}
  inline int GetMinDrainTime() const {return _min_drain_time;}
  inline deque<int> GetDrainTime() const {return _drain_time_q;}
  inline void WakeUp() {_wakeup_signal = true;}
  Router::ePowerState GetNeighborPowerState(int out_port) const;
  void IdleDetected();
  inline double GetPowerGateOverheadCycles() const {return _off_counter*_bet_threshold;}
  inline void SetDrainTag(int input) {_drain_tags[input] = true;}
  inline void SetRouterState(bool state) {_router_state = state;};
  // *************************************************************************

};

#endif
