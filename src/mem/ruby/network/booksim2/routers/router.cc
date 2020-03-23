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

/*router.cpp
 *
 *The base class of either iq router or event router
 *contains a list of channels and other router configuration variables
 *
 *The older version of the simulator uses an array of flits and credit to
 *simulate the channels. Newer version ueses flitchannel and credit channel
 *which can better model channel delay
 *
 *The older version of the simulator also uses vc_router and chaos router
 *which are replaced by iq rotuer and event router in the present form
 */

#include "mem/ruby/network/booksim2/booksim.hh"
#include <iostream>
#include <cassert>
#include "mem/ruby/network/booksim2/routers/router.hh"

//////////////////Sub router types//////////////////////
#include "mem/ruby/network/booksim2/routers/iq_router.hh"
#include "mem/ruby/network/booksim2/routers/event_router.hh"
#include "mem/ruby/network/booksim2/routers/chaos_router.hh"
/* ==== Power Gate - Begin ==== */
#include "mem/ruby/network/booksim2/routers/rflov_router.hh"
#include "mem/ruby/network/booksim2/routers/gflov_router.hh"
#include "mem/ruby/network/booksim2/routers/flov_router.hh"
#include "mem/ruby/network/booksim2/routers/rp_router.hh"
#include "mem/ruby/network/booksim2/routers/nord_router.hh"
/* ==== Power Gate - End ==== */
///////////////////////////////////////////////////////

/* ==== Power Gate - Begin ==== */
const char * const Router::POWERSTATE[] = {"power-off",
  "power-on", "draining", "wakeup", "invalid"};

int const Router::STALL_BUFFER_BUSY = -2;
int const Router::STALL_BUFFER_CONFLICT = -3;
int const Router::STALL_BUFFER_FULL = -4;
int const Router::STALL_BUFFER_RESERVED = -5;
int const Router::STALL_CROSSBAR_CONFLICT = -6;

Router::Router( const Configuration& config,
    Module *parent, const string & name, int id,
    int inputs, int outputs ) :
TimedModule( parent, name ), _id( id ), _inputs( inputs ), _outputs( outputs ),
   _partial_internal_cycles(0.0)
{
  _crossbar_delay   = ( config.GetInt( "st_prepare_delay" ) +
      config.GetInt( "st_final_delay" ) );
  _credit_delay     = config.GetInt( "credit_delay" );
  _input_speedup    = config.GetInt( "input_speedup" );
  _output_speedup   = config.GetInt( "output_speedup" );
  _internal_speedup = config.GetFloat( "internal_speedup" );
  _classes          = config.GetInt( "classes" );

#ifdef TRACK_FLOWS
  _received_flits.resize(_classes, vector<int>(_inputs, 0));
  _stored_flits.resize(_classes);
  _sent_flits.resize(_classes, vector<int>(_outputs, 0));
  _active_packets.resize(_classes);
  _outstanding_credits.resize(_classes, vector<int>(_outputs, 0));
#endif

#ifdef TRACK_STALLS
  _buffer_busy_stalls.resize(_classes, 0);
  _buffer_conflict_stalls.resize(_classes, 0);
  _buffer_full_stalls.resize(_classes, 0);
  _buffer_reserved_stalls.resize(_classes, 0);
  _crossbar_conflict_stalls.resize(_classes, 0);
#endif

  /* ==== Power Gate - Begin ==== */
  _power_state = power_on;
  _power_off_cycles = 0;
  _total_power_off_cycles = 0;
  _total_run_time = 0;
  _idle_threshold = config.GetInt( "idle_threshold" );
  _drain_threshold = config.GetInt( "drain_threshold" );
  _bet_threshold = config.GetInt( "bet_threshold" );
  _wakeup_threshold = config.GetInt( "wakeup_threshold" );
  _idle_timer = 0;
  _drain_timer = 0;
  _off_timer = 0;
  _wakeup_timer = 0;
  _wakeup_signal = false;
  _off_counter = 0;
  _drain_counter = 0;
  _drain_timeout_counter = 0;
  _max_drain_time = 0;
  _min_drain_time = -1;
  // FIXME: only support mesh now
  _neighbor_states.resize(4, power_on);
  _downstream_states.resize(4, power_on);
  _logical_neighbors.resize(4, -1);
  _logical_neighbors[0] = _id + 1;
  _logical_neighbors[1] = _id - 1;
  _logical_neighbors[2] = _id + gK;
  _logical_neighbors[3] = _id - gK;
  // for edge routers
  if (_id / gK == 0) {
    _neighbor_states[3] = power_off;
    _downstream_states[3] = power_off;
    _logical_neighbors[3] = -1;
  }
  if (_id / gK == gK-1) {
    _neighbor_states[2] = power_off;
    _downstream_states[2] = power_off;
    _logical_neighbors[2] = -1;
  }
  if (_id % gK == 0) {
    _neighbor_states[1] = power_off;
    _downstream_states[1] = power_off;
    _logical_neighbors[1] = -1;
  }
  if (_id % gK == gK-1) {
    _neighbor_states[0] = power_off;
    _downstream_states[0] = power_off;
    _logical_neighbors[0] = -1;
  }
  _outstanding_requests = 0;
  _router_state = true;
  _req_hids.resize(4, -1);
  _resp_hids.resize(4, -1);
  _watch_power_gating = false;
  /* ==== Power Gate - End ==== */
}

void Router::AddInputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  _input_channels.push_back( channel );
  _input_credits.push_back( backchannel );
  channel->SetSink( this, _input_channels.size() - 1 ) ;
}

void Router::AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  _output_channels.push_back( channel );
  _output_credits.push_back( backchannel );
  _channel_faults.push_back( false );
  channel->SetSource( this, _output_channels.size() - 1 ) ;
}

/* ==== Power Gate - Begin ==== */
void Router::AddInputHandshake( HandshakeChannel *channel )
{
  _input_handshakes.push_back(channel);
}

void Router::AddOutputHandshake( HandshakeChannel *channel )
{
  _output_handshakes.push_back(channel);
}
/* ==== Power Gate - End ==== */

void Router::Evaluate( )
{
  _partial_internal_cycles += _internal_speedup;
  while( _partial_internal_cycles >= 1.0 ) {
    _InternalStep( );
    _partial_internal_cycles -= 1.0;
  }
}

void Router::OutChannelFault( int c, bool fault )
{
  assert( ( c >= 0 ) && ( (size_t)c < _channel_faults.size( ) ) );

  _channel_faults[c] = fault;
}

bool Router::IsFaultyOutput( int c ) const
{
  assert( ( c >= 0 ) && ( (size_t)c < _channel_faults.size( ) ) );

  return _channel_faults[c];
}

/*Router constructor*/
Router *Router::NewRouter( const Configuration& config,
    Module *parent, const string & name, int id,
    int inputs, int outputs )
{
  const string type = config.GetStr( "router" );
  Router *r = NULL;
  if ( type == "iq" ) {
    r = new IQRouter( config, parent, name, id, inputs, outputs );
  } else if ( type == "event" ) {
    r = new EventRouter( config, parent, name, id, inputs, outputs );
  } else if ( type == "chaos" ) {
    r = new ChaosRouter( config, parent, name, id, inputs, outputs );
    /* ==== Power Gate - Begin ==== */
  } else if ( type == "flov" ) {
    r = new FLOVRouter( config, parent, name, id, inputs, outputs );
  } else if ( type == "rflov" ) {
    r = new RFLOVRouter( config, parent, name, id, inputs, outputs );
  } else if ( type == "gflov" ) {
    r = new GFLOVRouter( config, parent, name, id, inputs, outputs );
  } else if ( type == "rp" ) {
    r = new RPRouter( config, parent, name, id, inputs, outputs );
  } else if ( type == "nord" ) {
    r = new NoRDRouter( config, parent, name, id, inputs, outputs );
    /* ==== Power Gate - End ==== */
  } else {
    cerr << "Unknown router type: " << type << endl;
  }
  /*For additional router, add another else if statement*/
  /*Original booksim specifies the router using "flow_control"
   *we now simply call these types.
   */

  return r;
}

/* ==== Power Gate - Begin ==== */
void Router::IdleDetected()
{
  if (_power_state == power_on)
    ++_idle_timer;
  else if (_power_state == draining || _power_state == wakeup)
    ++_drain_timer;
  else if (_power_state == power_off)
    ++_off_timer;
}

Router * Router::GetNeighborRouter(int out_port)
{
  const FlitChannel * channel = _output_channels[out_port];
  Router * router = channel->GetSink();

  return router;
}

void Router::SetRingOutputVCBufferSize(int vc_buf_size) {};

void Router::SynchronizeCycle(uint64_t cycles)
{
    if (_power_state == power_on) {
        _idle_timer += cycles;
    } else if (_power_state == power_off) {
        _off_timer += cycles;
        _power_off_cycles += cycles;
        _total_power_off_cycles += cycles;
    }
}
/* ==== Power Gate - End ==== */


