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

#include "nord_router.hpp"

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cassert>
#include <limits>

#include "globals.hpp"
#include "random_utils.hpp"
#include "vc.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer.hpp"
#include "buffer_state.hpp"
#include "roundrobin_arb.hpp"
#include "allocator.hpp"
#include "switch_monitor.hpp"
#include "buffer_monitor.hpp"

NordRouter::NordRouter( Configuration const & config, Module *parent,
    string const & name, int id, int inputs, int outputs )
: IQRouter( config, parent, name, id, inputs, outputs )
{
  /* ==== Power Gate - Begin ==== */

  // forming Nord ring directions
  int row = _id / gK;
  int col = _id % gK;

  if (_id == 0) {
    _ring_in_port = DIR_SOUTH;
    _ring_out_port = DIR_EAST;
  } else if (_id == gK - 1) {
    _ring_in_port = DIR_WEST;
    _ring_out_port = DIR_SOUTH;
  } else if (row == 1 && col % 2 == 1 && col < gK - 1) {
    _ring_in_port = DIR_EAST;
    _ring_out_port = DIR_SOUTH;
  } else if (row == 1 && col % 2 == 0 && col > 0) {
    _ring_in_port = DIR_SOUTH;
    _ring_out_port = DIR_WEST;
  } else if (row == gK - 1 && col % 2 == 0) {
    _ring_in_port = DIR_EAST;
    _ring_out_port = DIR_NORTH;
  } else if (row == gK - 1 && col % 2 == 1) {
    _ring_in_port = DIR_NORTH;
    _ring_out_port = DIR_WEST;
  } else if (row == 0) {
    _ring_in_port = DIR_WEST;
    _ring_out_port = DIR_EAST;
  } else if (col % 2 == 0) {
    _ring_in_port = DIR_SOUTH;
    _ring_out_port = DIR_NORTH;
  } else {
    _ring_in_port = DIR_NORTH;
    _ring_out_port = DIR_SOUTH;
  }

  // Redefine next VCs' buffer state and size
  if (_power_state == power_off)
    _next_buf[_ring_out_port]->SetVCBufferSize(1);

  _handshake_buffer.resize(4);
  /* ==== Power Gate - End ==== */
}

NordRouter::~NordRouter( )
{
}

void NordRouter::ReadInputs( )
{
  bool have_flits = _ReceiveFlits( );
  bool have_credits = _ReceiveCredits( );
  /* ==== Power Gate - Begin ==== */
  _ReceiveHandshakes();
  // Should be evaluated before PowerStateEvaluate(), so put in ReadInputs()
  _HandshakeEvaluate();
  assert(_proc_handshakes.empty());
  /* ==== Power Gate - End ==== */
  _active = _active || have_flits || have_credits;
}

/* ==== Power Gate - Begin ==== */
void NordRouter::PowerStateEvaluate()
{
}
/* ==== Power Gate - End ==== */



void NordRouter::_InternalStep( )
{
  /* ==== Power Gate - Begin ==== */
  if (_power_state == power_off || _power_state == wakeup) {
    _NordStep();
    _OutputQueuing();
    assert(_out_queue_handshakes.empty());
    //_active = !_out_queue_handshakes.empty() || ...
    _active = !_proc_credits.empty() || !_in_queue_flits.empty();
    return;
  }
  /* ==== Power Gate - End ==== */

  if(!_active) {
    /* ==== Power Gate - Begin ==== */
    _HandshakeResponse();
    _OutputQueuing();
    assert(_out_queue_handshakes.empty());
    /* ==== Power Gate - End ==== */
    return;
  }

  _InputQueuing( );
  bool activity = !_proc_credits.empty();

  if(!_route_vcs.empty())
    _RouteEvaluate( );
  if(_vc_allocator) {
    _vc_allocator->Clear();
    if(!_vc_alloc_vcs.empty())
      _VCAllocEvaluate( );
  }
  if(_hold_switch_for_packet) {
    if(!_sw_hold_vcs.empty())
      _SWHoldEvaluate( );
  }
  _sw_allocator->Clear();
  if(_spec_sw_allocator)
    _spec_sw_allocator->Clear();
  if(!_sw_alloc_vcs.empty())
    _SWAllocEvaluate( );
  if(!_crossbar_flits.empty())
    _SwitchEvaluate( );

  if(!_route_vcs.empty()) {
    _RouteUpdate( );
    activity = activity || !_route_vcs.empty();
  }
  if(!_vc_alloc_vcs.empty()) {
    _VCAllocUpdate( );
    activity = activity || !_vc_alloc_vcs.empty();
  }
  if(_hold_switch_for_packet) {
    if(!_sw_hold_vcs.empty()) {
      _SWHoldUpdate( );
      activity = activity || !_sw_hold_vcs.empty();
    }
  }
  if(!_sw_alloc_vcs.empty()) {
    _SWAllocUpdate( );
    activity = activity || !_sw_alloc_vcs.empty();
  }
  if(!_crossbar_flits.empty()) {
    _SwitchUpdate( );
    activity = activity || !_crossbar_flits.empty();
  }
  /* ==== Power Gate - Begin ==== */
  _HandshakeResponse();

  //_active = activity;
  //flits are set back to RC in VC update
  _active = activity | !_route_vcs.empty();
  /* ==== Power Gate - End ==== */

  _OutputQueuing( );
  /* ==== Power Gate - Begin ==== */
  assert(_out_queue_handshakes.empty());
  /* ==== Power Gate - End ==== */

  _bufferMonitor->cycle( );
  _switchMonitor->cycle( );
}

void NordRouter::WriteOutputs( )
{
  _SendFlits( );
  _SendCredits( );
  /* ==== Power Gate - Begin ==== */
  _SendHandshakes( );
  /* ==== Power Gate - End ==== */
}


//------------------------------------------------------------------------------
// read inputs
//------------------------------------------------------------------------------

/* ==== Power Gate - Begin ==== */
void NordRouter::_ReceiveHandshakes()
{
  for (int input = 0; input < 4; ++input) {
    Handshake * const h = _input_handshakes[input]->Receive();
    if (h) {
      _proc_handshakes.push_back(make_pair(input, h));
    }
  }
}
/* ==== Power Gate - End ==== */

//------------------------------------------------------------------------------
// input queuing
//------------------------------------------------------------------------------

void NordRouter::_InputQueuing( )
{
  for(map<int, Flit *>::const_iterator iter = _in_queue_flits.begin();
      iter != _in_queue_flits.end();
      ++iter) {

    int const input = iter->first;
    assert((input >= 0) && (input < _inputs));

    Flit * const f = iter->second;
    assert(f);

    int const vc = f->vc;
    assert((vc >= 0) && (vc < _vcs));

    Buffer * const cur_buf = _buf[input];

    /* ==== Power Gate - Begin ==== */
    f->rtime = GetSimTime();  // enter router time
    /* ==== Power Gate - End ==== */

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
        << "Adding flit " << f->id
        << " to VC " << vc
        << " at input " << input
        << " (state: " << VC::VCSTATE[cur_buf->GetState(vc)];
      if(cur_buf->Empty(vc)) {
        *gWatchOut << ", empty";
      } else {
        assert(cur_buf->FrontFlit(vc));
        *gWatchOut << ", front: " << cur_buf->FrontFlit(vc)->id;
      }
      *gWatchOut << ")." << endl;
    }
    cur_buf->AddFlit(vc, f);

#ifdef TRACK_FLOWS
    ++_stored_flits[f->cl][input];
    if(f->head) ++_active_packets[f->cl][input];
#endif

    _bufferMonitor->write(input, f) ;

    if(cur_buf->GetState(vc) == VC::idle) {
      assert(cur_buf->FrontFlit(vc) == f);
      assert(cur_buf->GetOccupancy(vc) == 1);
      if (!f->head) {
        cout << GetSimTime() << " | " << FullName() << " | "
          << *f << endl;
      }
      assert(f->head);
      assert(_switch_hold_vc[input*_input_speedup + vc%_input_speedup] != vc);
      if(_routing_delay) {
        cur_buf->SetState(vc, VC::routing);
        _route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
      } else {
        if(f->watch) {
          *gWatchOut << GetSimTime() << " | " << FullName() << " | "
            << "Using precomputed lookahead routing information for VC " << vc
            << " at input " << input
            << " (front: " << f->id
            << ")." << endl;
        }
        cur_buf->SetRouteSet(vc, &f->la_route_set);
        cur_buf->SetState(vc, VC::vc_alloc);
        if(_speculative) {
          _sw_alloc_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc),
                  -1)));
        }
        if(_vc_allocator) {
          _vc_alloc_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc),
                  -1)));
        }
        if(_noq) {
          _UpdateNOQ(input, vc, f);
        }
      }
    } else if((cur_buf->GetState(vc) == VC::active) &&
        (cur_buf->FrontFlit(vc) == f)) {
      if(_switch_hold_vc[input*_input_speedup + vc%_input_speedup] == vc) {
        _sw_hold_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc),
                -1)));
      } else {
        _sw_alloc_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc),
                -1)));
      }
    }
  }
  _in_queue_flits.clear();

  while(!_proc_credits.empty()) {

    pair<int, pair<Credit *, int> > const & item = _proc_credits.front();

    int const time = item.first;
    if(GetSimTime() < time) {
      break;
    }

    Credit * const c = item.second.first;
    assert(c);

    int const output = item.second.second;
    assert((output >= 0) && (output < _outputs));

    BufferState * const dest_buf = _next_buf[output];

#ifdef TRACK_FLOWS
    for(set<int>::const_iterator iter = c->vc.begin(); iter != c->vc.end(); ++iter) {
      int const vc = *iter;
      assert(!_outstanding_classes[output][vc].empty());
      int cl = _outstanding_classes[output][vc].front();
      _outstanding_classes[output][vc].pop();
      assert(_outstanding_credits[cl][output] > 0);
      --_outstanding_credits[cl][output];
    }
#endif

    dest_buf->ProcessCredit(c);
    c->Free();
    _proc_credits.pop_front();
  }
}


//------------------------------------------------------------------------------
// routing
//------------------------------------------------------------------------------

void NordRouter::_RouteUpdate( )
{
  assert(_routing_delay);

  while(!_route_vcs.empty()) {

    pair<int, pair<int, int> > const & item = _route_vcs.front();

    int const time = item.first;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const input = item.second.first;
    assert((input >= 0) && (input < _inputs));
    int const vc = item.second.second;
    assert((vc >= 0) && (vc < _vcs));

    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::routing);

    Flit * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->vc == vc);
    assert(f->head);

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
        << "Completed routing for VC " << vc
        << " at input " << input
        << " (front: " << f->id
        << ")." << endl;
    }

    cur_buf->Route(vc, _rf, this, f, input);
    cur_buf->SetState(vc, VC::vc_alloc);
    /* ==== Power Gate - Begin ==== */
    // avoid rerouting by VCAlloc again and again
    f->rtime = GetSimTime();

    if (f->dest != _id) {
      OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
      assert(route_set);
      set<OutputSet::sSetElement> const setlist = route_set->GetSet();
      if (setlist.size() == 1) {
        set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
        int const out_port = iset->output_port;
        assert((out_port >= 0) && (out_port < _outputs));
        const FlitChannel * channel = _output_channels[out_port];
        Router * router = channel->GetSink();
        assert(router);
        if (f->dest == router->GetID()) {
          if (_neighbor_states[out_port] != power_on) { // what about draining, see the assertion below
            cout << GetSimTime() << " | router#" << _id << "'s neighbor router#"
              << router->GetID() << " is "
              << POWERSTATE[_neighbor_states[out_port]] << ", but actually is "
              << POWERSTATE[router->GetPowerState()] << "(flit " << f->id
              << " dest " << f->dest << ")" << endl;
            assert(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY);
            assert(0); // means should not come here??
          }
          assert(_neighbor_states[out_port] == power_on ||
              _neighbor_states[out_port] == draining);
        }
      }
    }
    /* ==== Power Gate - End ==== */
    if(_speculative) {
      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second, -1)));
    }
    if(_vc_allocator) {
      _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second, -1)));
    }
    // NOTE: No need to handle NOQ here, as it requires lookahead routing!
    _route_vcs.pop_front();
  }
}


//------------------------------------------------------------------------------
// VC allocation
//------------------------------------------------------------------------------

void NordRouter::_VCAllocUpdate( )
{
  assert(_vc_allocator);

  while(!_vc_alloc_vcs.empty()) {

    pair<int, pair<pair<int, int>, int> > const & item = _vc_alloc_vcs.front();

    int const time = item.first;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const input = item.second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const vc = item.second.first.second;
    assert((vc >= 0) && (vc < _vcs));

    assert(item.second.second != -1);

    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::vc_alloc);

    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->vc == vc);
    assert(f->head);

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
        << "Completed VC allocation for VC " << vc
        << " at input " << input
        << " (front: " << f->id
        << ")." << endl;
    }

    int const output_and_vc = item.second.second;

    if(output_and_vc >= 0) {

      int const match_output = output_and_vc / _vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _vcs;
      assert((match_vc >= 0) && (match_vc < _vcs));

      /* ==== Power Gate - Begin ==== */
      bool back_to_route = false;
      const FlitChannel * channel = _output_channels[match_output];
      Router * router = channel->GetSink();
      if (router) {
        const bool is_mc = (router->GetID() >= gNodes - gK);
        if (!is_mc && (_neighbor_states[match_output] == draining ||
              _neighbor_states[match_output] == wakeup))
          back_to_route = true;
        if (_neighbor_states[match_output] == power_off)
          assert(match_output == _ring_out_port);
      }
      if (!back_to_route) {
        if(f->watch) {
          *gWatchOut << GetSimTime() << " | " << FullName() << " | "
            << "  Acquiring assigned VC " << match_vc
            << " at output " << match_output
            << "." << endl;
        }

        BufferState * const dest_buf = _next_buf[match_output];
        assert(dest_buf->IsAvailableFor(match_vc));

        dest_buf->TakeBuffer(match_vc, input*_vcs + vc);

        cur_buf->SetOutput(vc, match_output, match_vc);
        cur_buf->SetState(vc, VC::active);
        if(!_speculative) {
          _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
        }
      } else {
        cur_buf->ClearRouteSet(vc);
        cur_buf->SetState(vc, VC::routing);
        _route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
        if (_speculative) {
          // should remove the speculative SA
          pair<int, int> input_vc = make_pair(input, vc);
          for (unsigned i = 0; i < _sw_alloc_vcs.size(); ++i) {
            pair<int, pair<pair<int, int>, int> > item = _sw_alloc_vcs[i];
            if (item.second.first == input_vc) {
              _sw_alloc_vcs.erase(_sw_alloc_vcs.begin()+i);
            }
          }
        }
        if (f->watch) {
          assert(f->head);
          *gWatchOut << GetSimTime() << " | " << FullName() << " | "
            << " Sink router " << router->GetID() << " is "
            << POWERSTATE[_neighbor_states[match_output]]
            << ", back to RC stage." << endl;

          cur_buf->Display(*gWatchOut);

          *gWatchOut << " route_vcs size: " << _route_vcs.size()
            << " time: " << _route_vcs.front().first
            << " input: " << _route_vcs.front().second.first
            << " vc: " << _route_vcs.front().second.second
            << " pid: " << f->pid << endl;

          *gWatchOut << " route_vcs empty?"
            << (_route_vcs.empty() ? "yes" : "no") << endl;
        }
      }
      /* ==== Power Gate - End ==== */
    } else {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
          << "  No output VC allocated." << endl;
      }

#ifdef TRACK_STALLS
      assert((output_and_vc == STALL_BUFFER_BUSY) ||
          (output_and_vc == STALL_BUFFER_CONFLICT));
      if(output_and_vc == STALL_BUFFER_BUSY) {
        ++_buffer_busy_stalls[f->cl];
      } else if(output_and_vc == STALL_BUFFER_CONFLICT) {
        ++_buffer_conflict_stalls[f->cl];
      }
#endif

      /* ==== Power Gate - Begin ==== */
      bool back_to_route = false;

      OutputSet const * route_set = cur_buf->GetRouteSet(vc);
      assert(route_set);

      set<OutputSet::sSetElement> setlist = route_set->GetSet();

      bool delete_route = false;
      set<OutputSet::sSetElement>::iterator iset = setlist.begin();
      while (iset != setlist.end()) {

        int const out_port = iset->output_port;
        assert((out_port >= 0) && (out_port < _outputs));

        const FlitChannel * channel = _output_channels[out_port];
        Router * router = channel->GetSink();
        if (router) {
          const bool is_mc = (router->GetID() >= gNodes - gK);
          if (!is_mc && (_neighbor_states[out_port] == draining ||
                _neighbor_states[out_port] == wakeup))
            delete_route = true;
          if (_neighbor_states[out_port] == power_off)
            assert(out_port == _ring_out_port);
        }

        if (delete_route) {
          set<OutputSet::sSetElement>::iterator iter = iset;
          ++iset;
          setlist.erase(iter);
        } else {
          ++iset;
        }
      }

      if (setlist.empty()) {
        back_to_route = true;
      }

      if (back_to_route) {
        cur_buf->ClearRouteSet(vc);
        cur_buf->SetState(vc, VC::routing);
        _route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
        if (_speculative) {
          // should remove the speculative SA
          pair<int, int> input_vc = make_pair(input, vc);
          for (unsigned i = 0; i < _sw_alloc_vcs.size(); ++i) {
            pair<int, pair<pair<int, int>, int> > item = _sw_alloc_vcs[i];
            if (item.second.first == input_vc) {
              _sw_alloc_vcs.erase(_sw_alloc_vcs.begin()+i);
            }
          }
        }
        _vc_alloc_vcs.pop_front();
        continue;
      }

      if (GetSimTime() - f->rtime == 300) { // timeout
        _buf[input]->SetState(vc, VC::routing);
        _route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
        if (_speculative) {
          pair<int, int> input_vc = make_pair(input, vc);
          for (unsigned i = 0; i < _sw_alloc_vcs.size(); ++i) {
            pair<int, pair<pair<int, int>, int> > item = _sw_alloc_vcs[i];
            if (item.second.first == input_vc) {
              _sw_alloc_vcs.erase(_sw_alloc_vcs.begin()+i);
              break;
            }
          }
        }
      } else {
        _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
      }
      /* ==== Power Gate - End ==== */
    }
    _vc_alloc_vcs.pop_front();
  }
}


//------------------------------------------------------------------------------
// switch holding
//------------------------------------------------------------------------------

void NordRouter::_SWHoldUpdate( )
{
  assert(_hold_switch_for_packet);

  while(!_sw_hold_vcs.empty()) {

    pair<int, pair<pair<int, int>, int> > const & item = _sw_hold_vcs.front();

    int const time = item.first;
    if(time < 0) {
      break;
    }
    assert(GetSimTime() == time);

    int const input = item.second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const vc = item.second.first.second;
    assert((vc >= 0) && (vc < _vcs));

    assert(item.second.second != -1);

    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::active);

    Flit * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->vc == vc);

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
        << "Completed held switch allocation for VC " << vc
        << " at input " << input
        << " (front: " << f->id
        << ")." << endl;
    }

    int const expanded_input = input * _input_speedup + vc % _input_speedup;
    assert(_switch_hold_vc[expanded_input] == vc);

    int const expanded_output = item.second.second;

    if(expanded_output >= 0 && ( _output_buffer_size==-1 || _output_buffer[expanded_output].size()<size_t(_output_buffer_size))) {

      assert(_switch_hold_in[expanded_input] == expanded_output);
      assert(_switch_hold_out[expanded_output] == expanded_input);

      int const output = expanded_output / _output_speedup;
      assert((output >= 0) && (output < _outputs));
      assert(cur_buf->GetOutputPort(vc) == output);

      int const match_vc = cur_buf->GetOutputVC(vc);
      assert((match_vc >= 0) && (match_vc < _vcs));

      BufferState * const dest_buf = _next_buf[output];

      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
          << "  Scheduling switch connection from input " << input
          << "." << (vc % _input_speedup)
          << " to output " << output
          << "." << (expanded_output % _output_speedup)
          << "." << endl;
      }

      cur_buf->RemoveFlit(vc);

#ifdef TRACK_FLOWS
      --_stored_flits[f->cl][input];
      if(f->tail) --_active_packets[f->cl][input];
#endif

      _bufferMonitor->read(input, f) ;

      f->hops++;
      f->vc = match_vc;

      if(!_routing_delay && f->head) {
        const FlitChannel * channel = _output_channels[output];
        const Router * router = channel->GetSink();
        if(router) {
          if(_noq) {
            if(f->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "Updating lookahead routing information for flit " << f->id
                << " (NOQ)." << endl;
            }
            int next_output_port = _noq_next_output_port[input][vc];
            assert(next_output_port >= 0);
            _noq_next_output_port[input][vc] = -1;
            int next_vc_start = _noq_next_vc_start[input][vc];
            assert(next_vc_start >= 0 && next_vc_start < _vcs);
            _noq_next_vc_start[input][vc] = -1;
            int next_vc_end = _noq_next_vc_end[input][vc];
            assert(next_vc_end >= 0 && next_vc_end < _vcs);
            _noq_next_vc_end[input][vc] = -1;
            f->la_route_set.Clear();
            f->la_route_set.AddRange(next_output_port, next_vc_start, next_vc_end);
          } else {
            if(f->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "Updating lookahead routing information for flit " << f->id
                << "." << endl;
            }
            int in_channel = channel->GetSinkPort();
            _rf(router, f, in_channel, &f->la_route_set, false);
          }
        } else {
          f->la_route_set.Clear();
        }
      }

#ifdef TRACK_FLOWS
      ++_outstanding_credits[f->cl][output];
      _outstanding_classes[output][f->vc].push(f->cl);
#endif

      dest_buf->SendingFlit(f);

      _crossbar_flits.push_back(make_pair(-1, make_pair(f, make_pair(expanded_input, expanded_output))));

      if(_out_queue_credits.count(input) == 0) {
        _out_queue_credits.insert(make_pair(input, Credit::New()));
      }
      _out_queue_credits[input]->vc.insert(vc);

      if(cur_buf->Empty(vc)) {
        if(f->watch) {
          *gWatchOut << GetSimTime() << " | " << FullName() << " | "
            << "  Cancelling held connection from input " << input
            << "." << (expanded_input % _input_speedup)
            << " to " << output
            << "." << (expanded_output % _output_speedup)
            << ": No more flits." << endl;
        }
        _switch_hold_vc[expanded_input] = -1;
        _switch_hold_in[expanded_input] = -1;
        _switch_hold_out[expanded_output] = -1;
        if(f->tail) {
          cur_buf->SetState(vc, VC::idle);
        }
      } else {
        Flit * const nf = cur_buf->FrontFlit(vc);
        assert(nf);
        assert(nf->vc == vc);
        if(f->tail) {
          assert(nf->head);
          if(f->watch) {
            *gWatchOut << GetSimTime() << " | " << FullName() << " | "
              << "  Cancelling held connection from input " << input
              << "." << (expanded_input % _input_speedup)
              << " to " << output
              << "." << (expanded_output % _output_speedup)
              << ": End of packet." << endl;
          }
          _switch_hold_vc[expanded_input] = -1;
          _switch_hold_in[expanded_input] = -1;
          _switch_hold_out[expanded_output] = -1;
          if(_routing_delay) {
            cur_buf->SetState(vc, VC::routing);
            /* ==== Power Gate - Begin ==== */
            nf->rtime = GetSimTime();
            /* ==== Power Gate - End ==== */
            _route_vcs.push_back(make_pair(-1, item.second.first));
          } else {
            if(nf->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "Using precomputed lookahead routing information for VC " << vc
                << " at input " << input
                << " (front: " << nf->id
                << ")." << endl;
            }
            cur_buf->SetRouteSet(vc, &nf->la_route_set);
            cur_buf->SetState(vc, VC::vc_alloc);
            if(_speculative) {
              _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
                      -1)));
            }
            if(_vc_allocator) {
              _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
                      -1)));
            }
            if(_noq) {
              _UpdateNOQ(input, vc, nf);
            }
          }
        } else {
          _sw_hold_vcs.push_back(make_pair(-1, make_pair(item.second.first,
                  -1)));
        }
      }
    } else {
      //when internal speedup >1.0, the buffer stall stats may not be accruate
      assert((expanded_output == STALL_BUFFER_FULL) ||
          (expanded_output == STALL_BUFFER_RESERVED) || !( _output_buffer_size==-1 || _output_buffer[expanded_output].size()<size_t(_output_buffer_size)));

      int const held_expanded_output = _switch_hold_in[expanded_input];
      assert(held_expanded_output >= 0);

      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
          << "  Cancelling held connection from input " << input
          << "." << (expanded_input % _input_speedup)
          << " to " << (held_expanded_output / _output_speedup)
          << "." << (held_expanded_output % _output_speedup)
          << ": Flit not sent." << endl;
      }
      _switch_hold_vc[expanded_input] = -1;
      _switch_hold_in[expanded_input] = -1;
      _switch_hold_out[held_expanded_output] = -1;
      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
              -1)));
    }
    _sw_hold_vcs.pop_front();
  }
}


//------------------------------------------------------------------------------
// switch allocation
//------------------------------------------------------------------------------

void NordRouter::_SWAllocUpdate( )
{
  while(!_sw_alloc_vcs.empty()) {

    pair<int, pair<pair<int, int>, int> > const & item = _sw_alloc_vcs.front();

    int const time = item.first;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const input = item.second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const vc = item.second.first.second;
    assert((vc >= 0) && (vc < _vcs));

    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert((cur_buf->GetState(vc) == VC::active) ||
        (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));

    Flit * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->vc == vc);

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
        << "Completed switch allocation for VC " << vc
        << " at input " << input
        << " (front: " << f->id
        << ")." << endl;
    }

    int const expanded_output = item.second.second;

    if(expanded_output >= 0) {

      int const expanded_input = input * _input_speedup + vc % _input_speedup;
      assert(_switch_hold_vc[expanded_input] < 0);
      assert(_switch_hold_in[expanded_input] < 0);
      assert(_switch_hold_out[expanded_output] < 0);

      int const output = expanded_output / _output_speedup;
      assert((output >= 0) && (output < _outputs));

      BufferState * const dest_buf = _next_buf[output];

      int match_vc;

      if(!_vc_allocator && (cur_buf->GetState(vc) == VC::vc_alloc)) {

        assert(f->head);

        int const cl = f->cl;
        assert((cl >= 0) && (cl < _classes));

        int const vc_offset = _vc_rr_offset[output*_classes+cl];

        match_vc = -1;
        int match_prio = numeric_limits<int>::min();

        const OutputSet * route_set = cur_buf->GetRouteSet(vc);
        set<OutputSet::sSetElement> const setlist = route_set->GetSet();

        assert(!_noq || (setlist.size() == 1));

        for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
            iset != setlist.end();
            ++iset) {
          if(iset->output_port == output) {

            int vc_start;
            int vc_end;

            if(_noq && _noq_next_output_port[input][vc] >= 0) {
              assert(!_routing_delay);
              vc_start = _noq_next_vc_start[input][vc];
              vc_end = _noq_next_vc_end[input][vc];
            } else {
              vc_start = iset->vc_start;
              vc_end = iset->vc_end;
            }
            assert(vc_start >= 0 && vc_start < _vcs);
            assert(vc_end >= 0 && vc_end < _vcs);
            assert(vc_end >= vc_start);

            for(int out_vc = vc_start; out_vc <= vc_end; ++out_vc) {
              assert((out_vc >= 0) && (out_vc < _vcs));

              int vc_prio = iset->pri;
              if(_vc_prioritize_empty && !dest_buf->IsEmptyFor(out_vc)) {
                assert(vc_prio >= 0);
                vc_prio += numeric_limits<int>::min();
              }

              // FIXME: This check should probably be performed in Evaluate(),
              // not Update(), as the latter can cause the outcome to depend on
              // the order of evaluation!
              if(dest_buf->IsAvailableFor(out_vc) &&
                  !dest_buf->IsFullFor(out_vc) &&
                  ((match_vc < 0) ||
                   RoundRobinArbiter::Supersedes(out_vc, vc_prio,
                     match_vc, match_prio,
                     vc_offset, _vcs))) {
                match_vc = out_vc;
                match_prio = vc_prio;
              }
            }
          }
        }
        assert(match_vc >= 0);

        if(f->watch) {
          *gWatchOut << GetSimTime() << " | " << FullName() << " | "
            << "  Allocating VC " << match_vc
            << " at output " << output
            << " via piggyback VC allocation." << endl;
        }

        cur_buf->SetState(vc, VC::active);
        cur_buf->SetOutput(vc, output, match_vc);
        dest_buf->TakeBuffer(match_vc, input*_vcs + vc);

        _vc_rr_offset[output*_classes+cl] = (match_vc + 1) % _vcs;

      } else {

        assert(cur_buf->GetOutputPort(vc) == output);

        match_vc = cur_buf->GetOutputVC(vc);

        /* ==== Power Gate - Begin ==== */
        // in SA, push back to RC if downstream router is draining
        if (f->head) {
          bool back_to_route = false;
          const FlitChannel * channel = _output_channels[output];
          Router * router = channel->GetSink();
          if (router) {
            const bool is_mc = (router->GetID() >= gNodes - gK);
            if (!is_mc && (_neighbor_states[output] == draining ||
                  _neighbor_states[output] == wakeup)) {
              back_to_route = true;
            }
            if (_downstream_states[output] == power_off)
              assert(output == _ring_out_port);
          }
          // donwstream is during power state transistion
          if (back_to_route) {
            if (f->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << " SA: Sink router " << router->GetID() << " is "
                << POWERSTATE[_neighbor_states[output]]
                << ", back to RC stage." << endl;
            }
            if (cur_buf->GetState(vc) == VC::vc_alloc) {
              assert(_speculative);
              pair<int, int> input_vc = make_pair(input, vc);
              for (unsigned i = 0; i < _vc_alloc_vcs.size(); ++i) {
                pair<int, pair<pair<int, int>, int> > item = _vc_alloc_vcs[i];
                if (item.second.first == input_vc) {
                  _vc_alloc_vcs.erase(_vc_alloc_vcs.begin()+i);
                  break;
                }
              }
            } else {
              assert(f->head);
              assert(cur_buf->GetState(vc) == VC::active);
              dest_buf->ReturnBuffer(match_vc);
            }
            cur_buf->ClearRouteSet(vc);
            cur_buf->SetState(vc, VC::routing);
            _route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
            _sw_alloc_vcs.pop_front();
            continue;
          }
        }
        /* ==== Power Gate - End ==== */
      }
      assert((match_vc >= 0) && (match_vc < _vcs));

      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
          << "  Scheduling switch connection from input " << input
          << "." << (vc % _input_speedup)
          << " to output " << output
          << "." << (expanded_output % _output_speedup)
          << "." << endl;
      }

      cur_buf->RemoveFlit(vc);

#ifdef TRACK_FLOWS
      --_stored_flits[f->cl][input];
      if(f->tail) --_active_packets[f->cl][input];
#endif

      _bufferMonitor->read(input, f) ;

      f->hops++;
      f->vc = match_vc;

      if(!_routing_delay && f->head) {
        const FlitChannel * channel = _output_channels[output];
        const Router * router = channel->GetSink();
        if(router) {
          if(_noq) {
            if(f->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "Updating lookahead routing information for flit " << f->id
                << " (NOQ)." << endl;
            }
            int next_output_port = _noq_next_output_port[input][vc];
            assert(next_output_port >= 0);
            _noq_next_output_port[input][vc] = -1;
            int next_vc_start = _noq_next_vc_start[input][vc];
            assert(next_vc_start >= 0 && next_vc_start < _vcs);
            _noq_next_vc_start[input][vc] = -1;
            int next_vc_end = _noq_next_vc_end[input][vc];
            assert(next_vc_end >= 0 && next_vc_end < _vcs);
            _noq_next_vc_end[input][vc] = -1;
            f->la_route_set.Clear();
            f->la_route_set.AddRange(next_output_port, next_vc_start, next_vc_end);
          } else {
            if(f->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "Updating lookahead routing information for flit " << f->id
                << "." << endl;
            }
            int in_channel = channel->GetSinkPort();
            _rf(router, f, in_channel, &f->la_route_set, false);
          }
        } else {
          f->la_route_set.Clear();
        }
      }

#ifdef TRACK_FLOWS
      ++_outstanding_credits[f->cl][output];
      _outstanding_classes[output][f->vc].push(f->cl);
#endif

      dest_buf->SendingFlit(f);

      _crossbar_flits.push_back(make_pair(-1, make_pair(f, make_pair(expanded_input, expanded_output))));

      if(_out_queue_credits.count(input) == 0) {
        _out_queue_credits.insert(make_pair(input, Credit::New()));
      }
      _out_queue_credits[input]->vc.insert(vc);

      if(cur_buf->Empty(vc)) {
        if(f->tail) {
          cur_buf->SetState(vc, VC::idle);
        }
      } else {
        Flit * const nf = cur_buf->FrontFlit(vc);
        assert(nf);
        assert(nf->vc == vc);
        if(f->tail) {
          assert(nf->head);
          if(_routing_delay) {
            cur_buf->SetState(vc, VC::routing);
            _route_vcs.push_back(make_pair(-1, item.second.first));
          } else {
            if(nf->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "Using precomputed lookahead routing information for VC " << vc
                << " at input " << input
                << " (front: " << nf->id
                << ")." << endl;
            }
            cur_buf->SetRouteSet(vc, &nf->la_route_set);
            cur_buf->SetState(vc, VC::vc_alloc);
            if(_speculative) {
              _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
                      -1)));
            }
            if(_vc_allocator) {
              _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
                      -1)));
            }
            if(_noq) {
              _UpdateNOQ(input, vc, nf);
            }
          }
        } else {
          if(_hold_switch_for_packet) {
            if(f->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "Setting up switch hold for VC " << vc
                << " at input " << input
                << "." << (expanded_input % _input_speedup)
                << " to output " << output
                << "." << (expanded_output % _output_speedup)
                << "." << endl;
            }
            _switch_hold_vc[expanded_input] = vc;
            _switch_hold_in[expanded_input] = expanded_output;
            _switch_hold_out[expanded_output] = expanded_input;
            _sw_hold_vcs.push_back(make_pair(-1, make_pair(item.second.first,
                    -1)));
          } else {
            _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
                    -1)));
          }
        }
      }
    } else {

      /* ==== Power Gate - Begin ==== */
      // in SA, push back to RC if downstream router is draining
      if (f->head) {

        bool back_to_route = false;

        int output = cur_buf->GetOutputPort(vc);
        int match_vc = cur_buf->GetOutputVC(vc);

        if (cur_buf->GetState(vc) == VC::vc_alloc) { // mis-speculation

          assert(_speculative);
          assert(output == -1);
          assert(match_vc == -1);

          OutputSet const * route_set = cur_buf->GetRouteSet(vc);
          assert(route_set);

          set<OutputSet::sSetElement> setlist = route_set->GetSet();

          bool delete_route = false;
          set<OutputSet::sSetElement>::iterator iset = setlist.begin();
          while (iset != setlist.end()) {

            int const out_port = iset->output_port;
            assert((out_port >= 0) && (out_port < _outputs));

            const FlitChannel * channel = _output_channels[out_port];
            Router * router = channel->GetSink();
            if (router) {
              const bool is_mc = (router->GetID() >= gNodes - gK);
              if (!is_mc && (_neighbor_states[out_port] == draining ||
                    _neighbor_states[out_port] == wakeup))
                delete_route = true;
              if (_neighbor_states[out_port] == power_off)
                assert(out_port == _ring_out_port);
            }

            if (delete_route) {
              set<OutputSet::sSetElement>::iterator iter = iset;
              ++iset;
              setlist.erase(iter);
            } else {
              ++iset;
            }
          }

          if (setlist.empty()) {
            back_to_route = true;
            pair<int, int> input_vc = make_pair(input, vc);
            for (unsigned i = 0; i < _vc_alloc_vcs.size(); ++i) {
              pair<int, pair<pair<int, int>, int> > item = _vc_alloc_vcs[i];
              if (item.second.first == input_vc) {
                _vc_alloc_vcs.erase(_vc_alloc_vcs.begin()+i);
                break;
              }
            }
          }
        } else { // mismatch or fail SA
          assert(cur_buf->GetState(vc) == VC::active);
          assert(output >= 0 && output < _outputs);
          assert(match_vc >= 0 && match_vc < _vcs);
          const FlitChannel * channel = _output_channels[output];
          Router * router = channel->GetSink();
          if (router) {
            const bool is_mc = (router->GetID() >= gNodes - gK);
            if (!is_mc && (_neighbor_states[output] == draining ||
                  _neighbor_states[output] == wakeup)) {
              back_to_route = true;
            }
            if (_neighbor_states[output] == power_off)
              assert(output == _ring_out_port);
          }
          if (back_to_route) {
            BufferState * dest_buf = _next_buf[output];
            dest_buf->ReturnBuffer(match_vc);
          }
        }

        // downstream is not power on
        if (back_to_route) {
          cur_buf->ClearRouteSet(vc);
          cur_buf->SetState(vc, VC::routing);
          _route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
          _sw_alloc_vcs.pop_front();
          continue;
        }
      }
      /* ==== Power Gate - End ==== */

      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
          << "  No output port allocated." << endl;
      }

#ifdef TRACK_STALLS
      assert((expanded_output == -1) || // for stalls that are accounted for in VC allocation path
          (expanded_output == STALL_BUFFER_BUSY) ||
          (expanded_output == STALL_BUFFER_CONFLICT) ||
          (expanded_output == STALL_BUFFER_FULL) ||
          (expanded_output == STALL_BUFFER_RESERVED) ||
          (expanded_output == STALL_CROSSBAR_CONFLICT));
      if(expanded_output == STALL_BUFFER_BUSY) {
        ++_buffer_busy_stalls[f->cl];
      } else if(expanded_output == STALL_BUFFER_CONFLICT) {
        ++_buffer_conflict_stalls[f->cl];
      } else if(expanded_output == STALL_BUFFER_FULL) {
        ++_buffer_full_stalls[f->cl];
      } else if(expanded_output == STALL_BUFFER_RESERVED) {
        ++_buffer_reserved_stalls[f->cl];
      } else if(expanded_output == STALL_CROSSBAR_CONFLICT) {
        ++_crossbar_conflict_stalls[f->cl];
      }
#endif

      /* ==== Power Gate - Begin ==== */
      int const input = item.second.first.first;
      assert((input >= 0) && (input < _inputs));
      int const vc = item.second.first.second;
      assert((vc >= 0) && (vc < _vcs));
      Buffer * const cur_buf = _buf[input];
      int const escape_vc = 0;
      if (GetSimTime() - f->rtime == 300 && f->head && cur_buf->GetOutputVC(vc) != escape_vc) { // timeout
        if (cur_buf->GetState(vc) == VC::active) {
          int const dest_output = cur_buf->GetOutputPort(vc);
          assert((dest_output >= 0) && (dest_output < _outputs));
          int const dest_vc = cur_buf->GetOutputVC(vc);
          assert((dest_vc >= 0) && (dest_vc < _vcs));
          BufferState * const dest_buf = _next_buf[dest_output];
          dest_buf->ReturnBuffer(dest_vc);
          _route_vcs.push_back(make_pair(-1, item.second.first));
          cur_buf->SetState(vc, VC::routing);
        } else {
          assert(cur_buf->GetState(vc) == VC::vc_alloc);
          int const dest_output = cur_buf->GetOutputPort(vc);
          assert(dest_output == -1);
          int const dest_vc = cur_buf->GetOutputVC(vc);
          assert(dest_vc == -1);
          pair<int, int> input_vc = make_pair(input, vc);
          for (unsigned i = 0; i < _vc_alloc_vcs.size(); ++i) {
            pair<int, pair<pair<int, int>, int> > item = _vc_alloc_vcs[i];
            if (item.second.first == input_vc) {
              _vc_alloc_vcs.erase(_vc_alloc_vcs.begin()+i);
              break;
            }
          }
          _route_vcs.push_back(make_pair(-1, item.second.first));
          cur_buf->SetState(vc, VC::routing);
        }
      } else {
        _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
      }
      /* ==== Power Gate - End ==== */
    }
    _sw_alloc_vcs.pop_front();
  }
}




//------------------------------------------------------------------------------
// output queuing
//------------------------------------------------------------------------------

void NordRouter::_OutputQueuing( )
{
  for(map<int, Credit *>::const_iterator iter = _out_queue_credits.begin();
      iter != _out_queue_credits.end();
      ++iter) {

    int const input = iter->first;
    assert((input >= 0) && (input < _inputs));

    Credit * const c = iter->second;
    assert(c);
    assert(!c->vc.empty());

    _credit_buffer[input].push(c);
  }
  _out_queue_credits.clear();

  /* ==== Power Gate - Begin ==== */
  for (map<int, Handshake *>::const_iterator iter =
       _out_queue_handshakes.begin(); iter != _out_queue_handshakes.end();
       ++iter) {

    int const output = iter->first;
    assert((output >= 0) && (output < 4));

    Handshake * const h = iter->second;
    assert(h);
    assert((h->new_state >= 0 || h->drain_done || h->wakeup) && h->id >= 0);

    _handshake_buffer[output].push(h);
  }
  _out_queue_handshakes.clear();
  /* ==== Power Gate - End ==== */
}

//------------------------------------------------------------------------------
// write outputs
//------------------------------------------------------------------------------

/* ==== Power Gate - Begin ==== */
void NordRouter::_SendHandshakes()
{
  for (int output = 0; output < 4; ++output) {
    if (!_handshake_buffer[output].empty()) {
      Handshake * const h = _handshake_buffer[output].front();
      assert(h);
      _handshake_buffer[output].pop();
      _output_handshakes[output]->Send(h);
    }
  }
}
/* ==== Power Gate - End ==== */


/* ==== Power Gate - Begin ==== */
//--------------------------------
// Nord Facilities
//--------------------------------


void NordRouter::_NordStep() {
  assert(_power_state == power_off || _power_state == wakeup);
  assert(_route_vcs.empty());
  assert(_vc_alloc_vcs.empty());
  assert(_sw_hold_vcs.empty());
  assert(_sw_alloc_vcs.empty());
  assert(_crossbar_flits.empty());
  if (_power_state == power_off && _off_timer == 1)
    assert(_in_queue_flits.empty());

  // process flits
  for (map<int, Flit *>::const_iterator iter = _in_queue_flits.begin();
       iter != _in_queue_flits.end(); ++iter) {

    int const input = iter->first;

    Flit * const f = iter->second;
    assert(f);

    int const vc = f->vc;
    assert((vc >= 0) && (vc < _vcs));

    int output;
    if (input == _ring_in_port) {
      output = DIR_NI;
      if (f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
          << "Bypass flit " << f->id << " to NI" << endl;
      }
    } else {
      assert(input == DIR_NI);
      output = _ring_out_port;
      if (f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
          << "Bypass flit " << f->id << " to next router" << endl;
      }
    }

    BufferState * const dest_buf = _next_buf[output];
    if (f->head)
        dest_buf->TakeBuffer(vc, _vcs * _inputs); // indicate its taken by nord
    dest_buf->SendingFlit(f);

    if (f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
      << "Buffering flit " << f->id << " at output " << output << "."
      << endl;
    }
    _output_buffer[output].push(f);

    f->hops++;
  }
  _in_queue_flits.clear();

  // off routers also process credits to make an image
  while (!_proc_credits.empty()) {

    pair<int, pair<Credit *, int> > const & item = _proc_credits.front();

    int const time = item.first;
    if (GetSimTime() < time) {
      break;
    }

    Credit * const c = item.second.first;
    assert(c);

    int const output = item.second.second;
    assert((output >= 0) && (output < _outputs));

    BufferState * const dest_buf = _next_buf[output];

#ifdef TRACK_FLOWS
    for(set<int>::const_iterator iter = c->vc.begin(); iter != c->vc.end(); ++iter) {
      int const vc = *iter;
      assert(!_outstanding_classes[output][vc].empty());
      int cl = _outstanding_classes[output][vc].front();
      _outstanding_classes[output][vc].pop();
      assert(_outstanding_credits[cl][output] > 0);
      --_outstanding_credits[cl][output];
    }
#endif

    dest_buf->ProcessCredit(c);
    // relay the credit to the upstream router
    // Nord channel input output mapping
    // ring_in_port --> NI
    // ring_out_port --> NI
    int input = -1;
    if (output == DIR_NI) {
      input = _ring_in_port;
    } else if (output == _ring_out_port) {
      input = DIR_NI;
    }

    if (input != -1) {
      if (_out_queue_credits.count(input) == 0) {
        _out_queue_credits.insert(make_pair(input, Credit::New()));
      }
      set<int>::iterator iter = c->vc.begin();
      while (iter != c->vc.end()) {

        int const vc = *iter;

        assert(vc >= 0 && vc < _vcs);
        _out_queue_credits[input]->vc.insert(vc);

        ++iter;
      }
    }

    c->Free();
    _proc_credits.pop_front();
  }
}

void NordRouter::_HandshakeEvaluate() {
  while (!_proc_handshakes.empty()) {
    pair<int, Handshake *> const & item = _proc_handshakes.front();
    int const input = item.first;
    int output = input;
    assert((input >= 0) && (input < 4));

    Handshake * h = item.second;
    assert(h);

    if (h->new_state == power_off) {
      if (_neighbor_states[output] != draining) {
        cout << GetSimTime() << output << "'s state: " << POWERSTATE[_neighbor_states[output]] << endl;
      }
      assert(_neighbor_states[output] == draining);
      assert(_drain_done_sent[output]);
      _drain_done_sent[output] = false;
      BufferState * dest_buf = _next_buf[output];
      dest_buf->ClearCredits();
      _neighbor_states[output] = (ePowerState) h->new_state;
    } else if (h->new_state == power_on && _neighbor_states[output] == wakeup) {
      _drain_done_sent[output] = false;
      BufferState * dest_buf = _next_buf[output];
      dest_buf->FullCredits();
      _neighbor_states[output] = (ePowerState) h->new_state;
    } else if (h->new_state == power_on && _neighbor_states[output] == draining) {
      _drain_done_sent[output] = false;
      _neighbor_states[output] = (ePowerState) h->new_state;
    } else if (h->new_state == draining || h->new_state == wakeup) {
      _drain_done_sent[output] = false;
      _neighbor_states[output] = (ePowerState) h->new_state;
    }

    if (h->drain_done) {
      assert(_power_state == draining || _power_state == wakeup || _power_state == power_on);
      if (h->hid == _req_hids[input] && (_power_state == draining || _power_state == wakeup)) {
        _drain_tags[input] = true;
      }
      /*if (_power_state == draining || _power_state == wakeup) {
        _drain_tags[input] = true;
      }*/
    } else {
      _resp_hids[output] = h->hid;
    }

    h->Free();
    _proc_handshakes.pop_front();
  }
}

void NordRouter::_HandshakeResponse() {
  assert(_power_state == power_on || _power_state == draining);

  for (int out_port = 0; out_port < 4; ++out_port) {
    if (_neighbor_states[out_port] == draining
        || _neighbor_states[out_port] == wakeup) {
      if (_drain_done_sent[out_port])
        continue;
      bool drain_done = true;
      int in_port;
      // check VCs
      for (int i = 1; i < _inputs; ++i) {
        in_port = (out_port + i) % _inputs;
        Buffer const * const cur_buf = _buf[in_port];
        for (int vc = 0; vc < _vcs; ++vc) {
          if (cur_buf->GetOutputPort(vc) == out_port
              && cur_buf->GetState(vc) == VC::active) {
            drain_done = false;
            break;
          }
        }
      }
      // check ST stage, crossbar_flits
      if (drain_done)
        for (deque<pair<int, pair<Flit *, pair<int, int> > > >::iterator iter =
             _crossbar_flits.begin(); iter != _crossbar_flits.end(); ++iter) {
          int const time = iter->first;
          assert(time <= GetSimTime());
          int const expanded_output = iter->second.second.second;
          int const output = expanded_output / _output_speedup;
          assert((output >= 0) && (output < _outputs));
          if (output == out_port) {
            drain_done = false;
            break;
          }
        }
      // check output buffer
      if (drain_done && !_output_buffer[out_port].empty())
        drain_done = false;
      // don't need to check link, because handshake has same delay as links

      if (drain_done) {
        assert(_neighbor_states[out_port] == draining ||
            _neighbor_states[out_port] == wakeup);
        assert(!_drain_done_sent[out_port]);
        if (_out_queue_handshakes.count(out_port) == 0) {
          _out_queue_handshakes.insert(make_pair(out_port, Handshake::New()));
        }
        _out_queue_handshakes[out_port]->drain_done = true;
        _out_queue_handshakes[out_port]->id = _id;
        _out_queue_handshakes[out_port]->hid = _resp_hids[out_port];
        _drain_done_sent[out_port] = true;
      }
    }
  }
}

void NordRouter::SetRingOutputVCBufferSize(int vc_buf_size)
{
  _next_buf[_ring_out_port]->SetVCBufferSize(vc_buf_size);
}
/* ==== Power Gate - End ==== */

