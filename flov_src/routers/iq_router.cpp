// $Id: iq_router.cpp 5263 2012-09-20 23:40:33Z dub $

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

#include "iq_router.hpp"

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

// Jiayi
#define POWERGATE
//#define DEBUG_POWERGATE
#define FLOV_FLOWCTRL

IQRouter::IQRouter(Configuration const & config, Module *parent,
		string const & name, int id, int inputs, int outputs) :
		Router(config, parent, name, id, inputs, outputs), _active(false) {
	_vcs = config.GetInt("num_vcs");

	_vc_busy_when_full = (config.GetInt("vc_busy_when_full") > 0);
	_vc_prioritize_empty = (config.GetInt("vc_prioritize_empty") > 0);
	_vc_shuffle_requests = (config.GetInt("vc_shuffle_requests") > 0);

	_speculative = (config.GetInt("speculative") > 0);
	_spec_check_elig = (config.GetInt("spec_check_elig") > 0);
	_spec_check_cred = (config.GetInt("spec_check_cred") > 0);
	_spec_mask_by_reqs = (config.GetInt("spec_mask_by_reqs") > 0);

	_routing_delay = config.GetInt("routing_delay");
	_vc_alloc_delay = config.GetInt("vc_alloc_delay");
	if (!_vc_alloc_delay) {
		Error("VC allocator cannot have zero delay.");
	}
	_sw_alloc_delay = config.GetInt("sw_alloc_delay");
	if (!_sw_alloc_delay) {
		Error("Switch allocator cannot have zero delay.");
	}

	// Routing
	string const rf = config.GetStr("routing_function") + "_"
			+ config.GetStr("topology");
	map<string, tRoutingFunction>::const_iterator rf_iter =
			gRoutingFunctionMap.find(rf);
	if (rf_iter == gRoutingFunctionMap.end()) {
		Error("Invalid routing function: " + rf);
	}
	_rf = rf_iter->second;

	// Alloc VC's
	_buf.resize(_inputs);
	for (int i = 0; i < _inputs; ++i) {
		ostringstream module_name;
		module_name << "buf_" << i;
		_buf[i] = new Buffer(config, _outputs, this, module_name.str());
		module_name.str("");
	}

	// Alloc next VCs' buffer state
	_next_buf.resize(_outputs);
	for (int j = 0; j < _outputs; ++j) {
		ostringstream module_name;
		module_name << "next_vc_o" << j;
		_next_buf[j] = new BufferState(config, this, module_name.str());
		module_name.str("");
	}
#ifdef FLOV_FLOWCTRL	// Alloc credit counter for FLOV flow control
	_credit_counter.resize(_outputs);
	for (int k = 0; k < _outputs; ++k) {
		_credit_counter[k].resize(_vcs, 0);
	}
	_clear_credits.resize(_outputs - 1, false);
	_full_credits.resize(_outputs - 1, false);
	_drain_done_sent.resize(_outputs - 1, false);
	_drain_tags.resize(_inputs - 1, false);
#endif

	// Alloc allocators
	string vc_alloc_type = config.GetStr("vc_allocator");
	if (vc_alloc_type == "piggyback") {
		if (!_speculative) {
			Error(
					"Piggyback VC allocation requires speculative switch allocation to be enabled.");
		}
		_vc_allocator = NULL;
		_vc_rr_offset.resize(_outputs * _classes, -1);
	} else {
		_vc_allocator = Allocator::NewAllocator(this, "vc_allocator", vc_alloc_type,
				_vcs * _inputs, _vcs * _outputs);

		if (!_vc_allocator) {
			Error("Unknown vc_allocator type: " + vc_alloc_type);
		}
	}

	string sw_alloc_type = config.GetStr("sw_allocator");
	_sw_allocator = Allocator::NewAllocator(this, "sw_allocator", sw_alloc_type,
			_inputs * _input_speedup, _outputs * _output_speedup);

	if (!_sw_allocator) {
		Error("Unknown sw_allocator type: " + sw_alloc_type);
	}

	string spec_sw_alloc_type = config.GetStr("spec_sw_allocator");
	if (_speculative && (spec_sw_alloc_type != "prio")) {
		_spec_sw_allocator = Allocator::NewAllocator(this, "spec_sw_allocator",
				spec_sw_alloc_type, _inputs * _input_speedup,
				_outputs * _output_speedup);
		if (!_spec_sw_allocator) {
			Error("Unknown spec_sw_allocator type: " + spec_sw_alloc_type);
		}
	} else {
		_spec_sw_allocator = NULL;
	}

	_sw_rr_offset.resize(_inputs * _input_speedup);
	for (int i = 0; i < _inputs * _input_speedup; ++i)
		_sw_rr_offset[i] = i % _input_speedup;

	_noq = config.GetInt("noq") > 0;
	if (_noq) {
		if (_routing_delay) {
			Error("NOQ requires lookahead routing to be enabled.");
		}
		if (_vcs < _outputs) {
			Error("NOQ requires at least as many VCs as router outputs.");
		}
	}
	_noq_next_output_port.resize(_inputs, vector<int>(_vcs, -1));
	_noq_next_vc_start.resize(_inputs, vector<int>(_vcs, -1));
	_noq_next_vc_end.resize(_inputs, vector<int>(_vcs, -1));

	// Output queues
	_output_buffer_size = config.GetInt("output_buffer_size");
	_output_buffer.resize(_outputs);
	_credit_buffer.resize(_inputs);
#ifdef HANDSHAKE
	_handshake_buffer.resize(_outputs - 1);
#endif

	// Switch configuration (when held for multiple cycles)
	_hold_switch_for_packet = (config.GetInt("hold_switch_for_packet") > 0);
	_switch_hold_in.resize(_inputs * _input_speedup, -1);
	_switch_hold_out.resize(_outputs * _output_speedup, -1);
	_switch_hold_vc.resize(_inputs * _input_speedup, -1);

	_bufferMonitor = new BufferMonitor(inputs, _classes);
	_switchMonitor = new SwitchMonitor(inputs, outputs, _classes);

#ifdef TRACK_FLOWS
	for(int c = 0; c < _classes; ++c) {
		_stored_flits[c].resize(_inputs, 0);
		_active_packets[c].resize(_inputs, 0);
	}
	_outstanding_classes.resize(_outputs, vector<queue<int> >(_vcs));
#endif
}

IQRouter::~IQRouter() {

	if (gPrintActivity) {
		cout << Name() << ".bufferMonitor:" << endl;
		cout << *_bufferMonitor << endl;

		cout << Name() << ".switchMonitor:" << endl;
		cout << "Inputs=" << _inputs;
		cout << "Outputs=" << _outputs;
		cout << *_switchMonitor << endl;
	}

	for (int i = 0; i < _inputs; ++i)
		delete _buf[i];

	for (int j = 0; j < _outputs; ++j)
		delete _next_buf[j];

	delete _vc_allocator;
	delete _sw_allocator;
	if (_spec_sw_allocator)
		delete _spec_sw_allocator;

	delete _bufferMonitor;
	delete _switchMonitor;
}

void IQRouter::AddOutputChannel(FlitChannel * channel,
		CreditChannel * backchannel) {
	int alloc_delay =
			_speculative ?
					max(_vc_alloc_delay, _sw_alloc_delay) :
					(_vc_alloc_delay + _sw_alloc_delay);
	int min_latency = 1 + _crossbar_delay + channel->GetLatency() + _routing_delay
			+ alloc_delay + backchannel->GetLatency() + _credit_delay;
	_next_buf[_output_channels.size()]->SetMinLatency(min_latency);
	Router::AddOutputChannel(channel, backchannel);
}

void IQRouter::ReadInputs() {
	bool have_flits = _ReceiveFlits();
	bool have_credits = _ReceiveCredits();
#ifdef HANDSHAKE	// Jiayi
	_ReceiveHandshakes();
	// Should be evaluated before PowerStateEvaluate(), so put in ReadInputs()
	_HandShakeEvaluate();
	assert(_proc_handshakes.empty());
#endif
	_active = _active || have_flits || have_credits;
}

// Jiayi
void IQRouter::PowerStateEvaluate() {

	if (_outstanding_requests) {
		assert(_power_state == power_on);
	}

	// MC routers are always on
	if (_id >= 56)
		_power_state = power_on;

	// power on
	else if (_power_state == power_on) {
		_drain_tags.clear();
		_drain_tags.resize(_inputs - 1, false);
		if (_outstanding_requests)
			if (_idle_timer > 0)
				--_idle_timer;
		if (_wakeup_signal == true) {
			_wakeup_signal = false;
			_idle_timer = 0;
		} else if (_router_state == false) {
//		} else if (_idle_timer >= _idle_threshold) {
			assert(_outstanding_requests == 0);
			bool neighbor_draining_wakeup = false;
			for (int out = 0; out < _outputs - 1; ++out) {
				if (_downstream_state[out] == draining
						|| _downstream_state[out] == wakeup) {
					neighbor_draining_wakeup = true;
				}
			}
			if (!neighbor_draining_wakeup) {
				_power_state = draining;
				_idle_timer = 0;
				_drain_timer = 0;
				++_drain_counter;
				_drain_tags.clear();
				_drain_tags.resize(_inputs - 1, false);
				assert(_out_queue_handshakes.empty());
				for (int out = 0; out < _outputs - 1; ++out) {
					if (_downstream_state[out] == power_off)
						_drain_tags[out] = true;
					if ((out == 0 && _id % 8 == 7) || (out == 1 && _id % 8 == 0)
							|| (out == 2 && _id / 8 == 7) || (out == 3 && _id / 8 == 0))
						continue;	// for edge routers
					_out_queue_handshakes.insert(make_pair(out, Handshake::New()));
					_out_queue_handshakes.find(out)->second->new_state = draining;
					_out_queue_handshakes.find(out)->second->src_state = _power_state;
					_out_queue_handshakes.find(out)->second->id = _id;
				}
			} else {
				--_idle_timer;// = 0;
			}
		}
	}

	// draining
	else if (_power_state == draining) {
		assert(_outstanding_requests == 0);
		bool neighbor_wakeup = false;
		bool neighbor_draining = false;
		for (int out = 0; out < _outputs - 1; ++out) {
			if (_downstream_state[out] == wakeup)
				neighbor_wakeup = true;
			if (_downstream_state[out] == draining)
				if (out == 1 || out == 3)	// They have higher priority
					neighbor_draining = true;
			if (neighbor_draining || neighbor_wakeup)
				break;
		}
		bool drain_done = _drain_tags[0] && _drain_tags[1] && _drain_tags[2]
				&& _drain_tags[3];
		drain_done &= _in_queue_flits.empty();
		drain_done &= _crossbar_flits.empty();
		for (int in_port = 0; in_port < _inputs; ++in_port) {
			Buffer const * const cur_buf = _buf[in_port];
			for (int vc = 0; vc < _vcs; ++vc) {
				if (cur_buf->GetState(vc) != VC::idle) {
					drain_done = false;
					break;
				}
			}
			drain_done &= _output_buffer[in_port].empty();
		}
		if (_wakeup_signal == true || neighbor_draining || neighbor_wakeup) {
			_wakeup_signal = false;
			_power_state = power_on;
			_drain_tags.clear();
			_drain_tags.resize(_inputs - 1, false);
			_idle_timer = 0;
			_drain_timer = 0;
			assert(_out_queue_handshakes.empty());
			for (int out = 0; out < _outputs - 1; ++out) {
				if ((out == 0 && _id % 8 == 7) || (out == 1 && _id % 8 == 0)
						|| (out == 2 && _id / 8 == 7) || (out == 3 && _id / 8 == 0))
					continue;	// for edge routers
				_out_queue_handshakes.insert(make_pair(out, Handshake::New()));
				_out_queue_handshakes.find(out)->second->new_state = power_on;
				_out_queue_handshakes.find(out)->second->src_state = _power_state;
				_out_queue_handshakes.find(out)->second->id = _id;
			}
		} else if (drain_done) {
			for (int i = 0; i < 4; ++i) {
				if ((i == 0 && _id % 8 == 0) || (i == 1 && _id % 8 == 7)
						|| (i == 2 && _id / 8 == 0) || (i == 3 && _id / 8 == 7))
					continue;
				const BufferState * dest_buf = _next_buf[i];
				for (int vc = 0; vc < _vcs; ++vc) {
					int credit_count = dest_buf->AvailableFor(vc);
					assert(credit_count >= 0);
					_credit_counter[i][vc] = credit_count;
				}
			}
			_power_state = power_off;
			_drain_tags.clear();
			_drain_tags.resize(_inputs - 1, false);
			_off_timer = 0;
			assert(_out_queue_handshakes.empty());
			for (int out = 0; out < _outputs - 1; ++out) {
				if ((out == 0 && _id % 8 == 7) || (out == 1 && _id % 8 == 0)
						|| (out == 2 && _id / 8 == 7) || (out == 3 && _id / 8 == 0))
					continue;	// for edge routers
				int in = out;
				if (out % 2)
					--in;
				else
					++in;
				_out_queue_handshakes.insert(make_pair(out, Handshake::New()));
				_out_queue_handshakes.find(out)->second->new_state =
						_downstream_state[in];
				_out_queue_handshakes.find(out)->second->src_state = _power_state;
				_out_queue_handshakes.find(out)->second->id = _id;
			}
			_drain_time_q.push_back(_drain_timer);
			if (_max_drain_time < _drain_timer)
				_max_drain_time = _drain_timer;
			if (_min_drain_time > _drain_timer || _min_drain_time == -1)
				_min_drain_time = _drain_timer;
			_drain_timer = 0;
		} else if (_drain_timer > _drain_threshold) {
			_power_state = power_on;
			_drain_tags.clear();
			_drain_tags.resize(_inputs - 1, false);
			_idle_timer = 0;
			assert(_out_queue_handshakes.empty());
			for (int out = 0; out < _outputs - 1; ++out) {
				if ((out == 0 && _id % 8 == 7) || (out == 1 && _id % 8 == 0)
						|| (out == 2 && _id / 8 == 7) || (out == 3 && _id / 8 == 0))
					continue;	// for edge routers
				_out_queue_handshakes.insert(make_pair(out, Handshake::New()));
				_out_queue_handshakes.find(out)->second->new_state = power_on;
				_out_queue_handshakes.find(out)->second->src_state = _power_state;
				_out_queue_handshakes.find(out)->second->id = _id;
			}
			if (_drain_timer > _drain_threshold) {
				++_drain_timeout_counter;
			}
			_drain_time_q.push_back(_drain_timer);
			if (_max_drain_time < _drain_timer)
				_max_drain_time = _drain_timer;
			if (_min_drain_time > _drain_timer || _min_drain_time == -1)
				_min_drain_time = _drain_timer;
			_drain_timer = 0;
		}
	}

	// power off
	else if (_power_state == power_off) {
		_drain_tags.clear();
		_drain_tags.resize(_inputs - 1, false);
		for (int in_port = 0; in_port < _inputs; ++in_port) {
			Buffer const * const cur_buf = _buf[in_port];
			for (int vc = 0; vc < _vcs; ++vc) {
				assert(cur_buf->GetState(vc) == VC::idle);
			}
		}
		++_power_off_cycles;
		++_total_power_off_cycles;
//		if (_wakeup_signal == true) {
		if (_router_state) {
			++_off_timer;
			bool neighbor_wakeup = false;
			bool neighbor_draining = false;
			for (int out = 0; out < _outputs - 1; ++out) {
				if (_downstream_state[out] == wakeup) {
					neighbor_wakeup = true;
					break;
				} else if (_downstream_state[out] == draining) {
					neighbor_draining = true;
					break;
				}
			}
//			if (!neighbor_wakeup && _off_timer >= _bet_threshold) {
			// NOTE: if I have handshake to relay, I should delay my own handshake.
			//			 Otherwise, if the handshake to be relay is for state change from
			//			 draining->off, the credit is not initialized while my own state
			//			 change off->wakeup is sent, credit may overflow
			if (!neighbor_wakeup && _off_timer >= _bet_threshold && _out_queue_handshakes.empty() && !neighbor_draining) {
				_wakeup_signal = false;
				_power_state = wakeup;
				_wakeup_timer = 0;
				_off_timer = 0;
				++_off_counter; // for power gating overhead
				_drain_tags.clear();
				_drain_tags.resize(_inputs - 1, false);
				assert(_out_queue_handshakes.empty());
				for (int out = 0; out < _outputs - 1; ++out) {
					if (_downstream_state[out] == power_off)
						_drain_tags[out] = true;
					if ((out == 0 && _id % 8 == 7) || (out == 1 && _id % 8 == 0)
							|| (out == 2 && _id / 8 == 7) || (out == 3 && _id / 8 == 0))
						continue;	// for edge routers
					_out_queue_handshakes.insert(make_pair(out, Handshake::New()));
					_out_queue_handshakes.find(out)->second->new_state = wakeup;
					_out_queue_handshakes.find(out)->second->src_state = _power_state;
					_out_queue_handshakes.find(out)->second->id = _id;
				}
			}
		}
	}

	// wakeup
	else if (_power_state == wakeup) {
		for (int in_port = 0; in_port < _inputs; ++in_port) {
			Buffer const * const cur_buf = _buf[in_port];
			for (int vc = 0; vc < _vcs; ++vc) {
				assert(cur_buf->GetState(vc) == VC::idle);
			}
		}
		for (int out = 0; out < _outputs-1; ++out) {
			if (_downstream_state[out] == power_off)
				_drain_tags[out] = true;
		}
		// don't consider draining since wakeup has higher priority
		// NOTE: can wake up at the same time, the handshake relaying is done
		// in _HandShakeEvaluate()
		bool drain_done = _drain_tags[0] && _drain_tags[1] && _drain_tags[2]
				&& _drain_tags[3];
		drain_done &= _in_queue_flits.empty();
		++_wakeup_timer;
//		if (drain_done && _wakeup_timer >= _wakeup_threshold) {
		// NOTE: if I have handshake to relay, I should delay my own handshake
		if (drain_done && _wakeup_timer >= _wakeup_threshold && _out_queue_handshakes.empty()) {
			_wakeup_signal = false;
			_wakeup_timer = 0;
			_idle_timer = 0;
			_power_state = power_on;
			_drain_tags.clear();
			_drain_tags.resize(_inputs - 1, false);
			// _out_queue_handshakes don't need to empty when need
			// to relay drain_tag for downstream waking up routers
			assert(_out_queue_handshakes.empty());
			for (int out = 0; out < _outputs - 1; ++out) {
				if ((out == 0 && _id % 8 == 7) || (out == 1 && _id % 8 == 0)
						|| (out == 2 && _id / 8 == 7) || (out == 3 && _id / 8 == 0))
					continue;	// for edge routers
				if (_out_queue_handshakes.count(out) == 0)
					_out_queue_handshakes.insert(make_pair(out, Handshake::New()));
				_out_queue_handshakes.find(out)->second->new_state = power_on;
				_out_queue_handshakes.find(out)->second->src_state = _power_state;
				_out_queue_handshakes.find(out)->second->id = _id;
			}
			// set drain_done_tag based on my downstream information
			for (int out = 0; out < _outputs-1; ++out) {
				if (_downstream_state[out] == wakeup || _downstream_state[out] == draining) {
					int opp_out = out;
					if (out % 2)
						--opp_out;
					else
						++opp_out;
					if (_downstream_state[opp_out] == power_off) {
						_drain_done_sent[out] = true;
					}
				}
			}
		}
	}

	// never reach
	else
		assert(0);
}

void IQRouter::_InternalStep() {

#ifdef DEBUG_POWERGATE
//	if (_id == 1 || _id == 2 || _id == 3 || _id == 4) {
//		cout << GetSimTime() << " | " << FullName() << " | state: "
//			<< POWERSTATE[_power_state] << ", neighbor#0 state: "
//			<< POWERSTATE[_neighbor_state[0]] << ", downstream#2 state: "
//			<< POWERSTATE[_downstream_state[0]] << endl;
//		if (_id == 2) {
//			cout << " - credit count for next buffer#0: " << _credit_counter[0][0]
//			    << " " << _credit_counter[0][1] << " " << _credit_counter[0][2]
//			    << " " << _credit_counter[0][3] << endl;
//		}
//		//if (_id == 2 || _id == 3)
//			_next_buf[0]->Display(cout);
//		if (_id == 4)
//			_buf[1]->Display(cout);
//	}
#endif

  if (GetSimTime() == 274621 && _id == 12) {
    cout << "WOW: out put port is " << _buf[3]->GetOutputPort(3) << endl;
  }

#ifdef DEBUG_POWERGATE
	if (_id == 31)
		cout << GetSimTime() << " | router#" << _id << " downstream 3 is " << POWERSTATE[_downstream_state[3]] << endl;
	if ((_id == 23 || _id == 43) && _power_state == draining) {// && _power_state == draining) {//GetSimTime() >= 100275) {
		//assert(_power_state == wakeup);
		cout << GetSimTime() << " | router#" << _id << " state: " << POWERSTATE[_power_state] << " drain tags: "
				<< _drain_tags[0] << ' ' << _drain_tags[1] << ' '
				<< _drain_tags[2] << ' ' << _drain_tags[3] << endl;
	}
	if (GetSimTime() >= 101822)
		assert(0);
#endif

#ifdef FLOV_FLOWCTRL
	if (_power_state == power_off || _power_state == wakeup) {
		_FlovStep();
		_OutputQueuing();
		assert(_out_queue_handshakes.empty());
		_active = !_out_queue_handshakes.empty() || !_proc_credits.empty()
				|| !_in_queue_flits.empty();
		return;
	}
#endif

	if (!_active) {
		_HandShakeResponse();
		_OutputQueuing();
		assert(_out_queue_handshakes.empty());
		return;
	}

	_InputQueuing();
	bool activity = !_proc_credits.empty();

	if (!_route_vcs.empty())
		_RouteEvaluate();
	if (_vc_allocator) {
		_vc_allocator->Clear();
		if (!_vc_alloc_vcs.empty())
			_VCAllocEvaluate();
	}
	if (_hold_switch_for_packet) {
		if (!_sw_hold_vcs.empty())
			_SWHoldEvaluate();
	}
	_sw_allocator->Clear();
	if (_spec_sw_allocator)
		_spec_sw_allocator->Clear();
	if (!_sw_alloc_vcs.empty())
		_SWAllocEvaluate();
	if (!_crossbar_flits.empty())
		_SwitchEvaluate();

	if (!_route_vcs.empty()) {
		_RouteUpdate();
		activity = activity || !_route_vcs.empty();
	}
	if (!_vc_alloc_vcs.empty()) {
		_VCAllocUpdate();
		activity = activity || !_vc_alloc_vcs.empty();
	}
	if (_hold_switch_for_packet) {
		if (!_sw_hold_vcs.empty()) {
			_SWHoldUpdate();
			activity = activity || !_sw_hold_vcs.empty();
		}
	}
	if (!_sw_alloc_vcs.empty()) {
		_SWAllocUpdate();
		activity = activity || !_sw_alloc_vcs.empty();
	}
	if (!_crossbar_flits.empty()) {
		_SwitchUpdate();
		activity = activity || !_crossbar_flits.empty();
	}
	_HandShakeResponse();	// Jiayi

	//_active = activity;
	_active = activity | !_route_vcs.empty(); // Jiayi, flits are set back to RC in VA update
	_OutputQueuing();
	assert(_out_queue_handshakes.empty());

	_bufferMonitor->cycle();
	_switchMonitor->cycle();

}

void IQRouter::WriteOutputs() {
	_SendFlits();
	_SendCredits();
#ifdef HANDSHAKE
	_SendHandshakes();
#endif
}

//------------------------------------------------------------------------------
// read inputs
//------------------------------------------------------------------------------

bool IQRouter::_ReceiveFlits() {
	bool activity = false;
	for (int input = 0; input < _inputs; ++input) {
		Flit * const f = _input_channels[input]->Receive();
		if (f) {

#ifdef TRACK_FLOWS
			++_received_flits[f->cl][input];
#endif

			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "Received flit " << f->id << " from channel at input " << input
						<< "." << endl;
			}
			_in_queue_flits.insert(make_pair(input, f));
			activity = true;
		}
	}
	return activity;
}

bool IQRouter::_ReceiveCredits() {
	bool activity = false;
	for (int output = 0; output < _outputs; ++output) {
		Credit * const c = _output_credits[output]->Receive();
		if (c) {
			_proc_credits.push_back(
					make_pair(GetSimTime() + _credit_delay, make_pair(c, output)));
			activity = true;
		}
	}
	return activity;
}

void IQRouter::_ReceiveHandshakes() {	// Jiayi
	for (int input = 0; input < _inputs - 1; ++input) {
		Handshake * const h = _input_handshakes[input]->Receive();
		if (h) {
			_proc_handshakes.push_back(make_pair(input, h));
		}
	}
}

//------------------------------------------------------------------------------
// input queuing
//------------------------------------------------------------------------------

void IQRouter::_InputQueuing() {
	for (map<int, Flit *>::const_iterator iter = _in_queue_flits.begin();
			iter != _in_queue_flits.end(); ++iter) {

		int const input = iter->first;
		assert((input >= 0) && (input < _inputs));

		Flit * const f = iter->second;
		assert(f);

		int const vc = f->vc;
		assert((vc >= 0) && (vc < _vcs));

		Buffer * const cur_buf = _buf[input];

		f->rtime = GetSimTime(); // Jiayi, enter router time

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Adding flit " << f->id << " to VC " << vc << " at input " << input
					<< " (state: " << VC::VCSTATE[cur_buf->GetState(vc)];
			if (cur_buf->Empty(vc)) {
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

#ifdef POWERGATE
//		if (f->head) {
//			if (f->src == _id && _id < 56) {
//				assert(f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST);
//				if (f->head)
//					++_outstanding_requests;
//			} else if (f->src == _id && _id >= 56) {
//				assert(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY);
//			} else if (f->dest == _id && _id < 56) {
//				assert(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY);
//				if (f->head)
//					--_outstanding_requests;
//			} else if (f->dest == _id && _id >= 56)
//				assert(f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST);
//		}
#endif

		_bufferMonitor->write(input, f);

		if (cur_buf->GetState(vc) == VC::idle) {
			assert(cur_buf->FrontFlit(vc) == f);
			assert(cur_buf->GetOccupancy(vc) == 1);
			assert(f->head);
			assert(
					_switch_hold_vc[input * _input_speedup + vc % _input_speedup] != vc);
			if (_routing_delay) {
				cur_buf->SetState(vc, VC::routing);
				_route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
			} else {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "Using precomputed lookahead routing information for VC " << vc
							<< " at input " << input << " (front: " << f->id << ")." << endl;
				}
				cur_buf->SetRouteSet(vc, &f->la_route_set);
				cur_buf->SetState(vc, VC::vc_alloc);
				if (_speculative) {
					_sw_alloc_vcs.push_back(
							make_pair(-1, make_pair(make_pair(input, vc), -1)));
				}
				if (_vc_allocator) {
					_vc_alloc_vcs.push_back(
							make_pair(-1, make_pair(make_pair(input, vc), -1)));
				}
				if (_noq) {
					_UpdateNOQ(input, vc, f);
				}
			}
		} else if ((cur_buf->GetState(vc) == VC::active)
				&& (cur_buf->FrontFlit(vc) == f)) {
			if (_switch_hold_vc[input * _input_speedup + vc % _input_speedup] == vc) {
				_sw_hold_vcs.push_back(
						make_pair(-1, make_pair(make_pair(input, vc), -1)));
			} else {
				_sw_alloc_vcs.push_back(
						make_pair(-1, make_pair(make_pair(input, vc), -1)));
			}
		}
	}
	_in_queue_flits.clear();

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
		c->Free();
		_proc_credits.pop_front();
	}
}

//------------------------------------------------------------------------------
// routing
//------------------------------------------------------------------------------

void IQRouter::_RouteEvaluate() {
	assert(_routing_delay);

	for (deque<pair<int, pair<int, int> > >::iterator iter = _route_vcs.begin();
			iter != _route_vcs.end(); ++iter) {

		int const time = iter->first;
		if (time >= 0) {
			break;
		}
		iter->first = GetSimTime() + _routing_delay - 1;

		int const input = iter->second.first;
		assert((input >= 0) && (input < _inputs));
		int const vc = iter->second.second;
		assert((vc >= 0) && (vc < _vcs));

		Buffer const * const cur_buf = _buf[input];
		assert(!cur_buf->Empty(vc));
		assert(cur_buf->GetState(vc) == VC::routing);

		Flit const * const f = cur_buf->FrontFlit(vc);
		assert(f);
		assert(f->vc == vc);
		assert(f->head);

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Beginning routing for VC " << vc << " at input " << input
					<< " (front: " << f->id << ")." << endl;
		}
	}
}

void IQRouter::_RouteUpdate() {
	assert(_routing_delay);

	while (!_route_vcs.empty()) {

		pair<int, pair<int, int> > const & item = _route_vcs.front();

		int const time = item.first;
		if ((time < 0) || (GetSimTime() < time)) {
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

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Completed routing for VC " << vc << " at input " << input
					<< " (front: " << f->id << ")." << endl;
		}

		cur_buf->Route(vc, _rf, this, f, input);
		cur_buf->SetState(vc, VC::vc_alloc);
		f->rtime = GetSimTime(); // Jiayi

#ifdef POWERGATE
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
					if (_neighbor_state[out_port] != power_on) {
						cout << GetSimTime() << " | router#" << _id << "'s neighbor router#"
								<< router->GetID() << " is "
								<< POWERSTATE[_neighbor_state[out_port]] << ", but actually is "
								<< POWERSTATE[router->GetPowerState()] << "(flit " << f->id
								<< " dest " << f->dest << ")" << endl;
						assert(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY);
						assert(0);
					}
					assert(
							_neighbor_state[out_port] == power_on
									|| _neighbor_state[out_port] == draining);
				}
			}
		}
#endif
		if (_speculative) {
			_sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second, -1)));
		}
		if (_vc_allocator) {
			_vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second, -1)));
		}
		// NOTE: No need to handle NOQ here, as it requires lookahead routing!
		_route_vcs.pop_front();
	}
}

//------------------------------------------------------------------------------
// VC allocation
//------------------------------------------------------------------------------

void IQRouter::_VCAllocEvaluate() {
	assert(_vc_allocator);

	bool watched = false;

	for (deque<pair<int, pair<pair<int, int>, int> > >::iterator iter =
			_vc_alloc_vcs.begin(); iter != _vc_alloc_vcs.end(); ++iter) {

		int const time = iter->first;
		if (time >= 0) {
			break;
		}

		int const input = iter->second.first.first;
		assert((input >= 0) && (input < _inputs));
		int const vc = iter->second.first.second;
		assert((vc >= 0) && (vc < _vcs));

		assert(iter->second.second == -1);

		Buffer const * const cur_buf = _buf[input];
		assert(!cur_buf->Empty(vc));
		assert(cur_buf->GetState(vc) == VC::vc_alloc);

		Flit const * const f = cur_buf->FrontFlit(vc);
		assert(f);
		assert(f->vc == vc);
		assert(f->head);

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Beginning VC allocation for VC " << vc << " at input " << input
					<< " (front: " << f->id << ")." << endl;
		}

//		if (GetSimTime() - f->rtime == 300) // Jiayi, timeout, may need escape
//			_buf[input]->Route(vc, _rf, this, f, input);

		OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
		assert(route_set);

		int const out_priority = cur_buf->GetPriority(vc);
		set<OutputSet::sSetElement> const setlist = route_set->GetSet();

		bool elig = false;
		bool cred = false;
		bool reserved = false;

		assert(!_noq || (setlist.size() == 1));

		for (set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
				iset != setlist.end(); ++iset) {

			int const out_port = iset->output_port;
			assert((out_port >= 0) && (out_port < _outputs));

			BufferState const * const dest_buf = _next_buf[out_port];

			int vc_start;
			int vc_end;

			if (_noq && _noq_next_output_port[input][vc] >= 0) {
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

			for (int out_vc = vc_start; out_vc <= vc_end; ++out_vc) {
				assert((out_vc >= 0) && (out_vc < _vcs));

				int in_priority = iset->pri;
				if (_vc_prioritize_empty && !dest_buf->IsEmptyFor(out_vc)) {
					assert(in_priority >= 0);
					in_priority += numeric_limits<int>::min();
				}

				// On the input input side, a VC might request several output VCs.
				// These VCs can be prioritized by the routing function, and this is
				// reflected in "in_priority". On the output side, if multiple VCs are
				// requesting the same output VC, the priority of VCs is based on the
				// actual packet priorities, which is reflected in "out_priority".

				if (!dest_buf->IsAvailableFor(out_vc)) {
					if (f->watch) {
						int const use_input_and_vc = dest_buf->UsedBy(out_vc);
						int const use_input = use_input_and_vc / _vcs;
						int const use_vc = use_input_and_vc % _vcs;
						*gWatchOut << GetSimTime() << " | " << FullName() << " | "
								<< "  VC " << out_vc << " at output " << out_port
								<< " is in use by VC " << use_vc << " at input " << use_input;
						Flit * cf = _buf[use_input]->FrontFlit(use_vc);
						if (cf) {
							*gWatchOut << " (front flit: " << cf->id << ")";
						} else {
							*gWatchOut << " (empty)";
						}
						*gWatchOut << "." << endl;
					}
				} else {
					elig = true;
					if (_vc_busy_when_full && dest_buf->IsFullFor(out_vc)) {
						if (f->watch)
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "  VC " << out_vc << " at output " << out_port
									<< " is full." << endl;
						reserved |= !dest_buf->IsFull();
					} else {
						cred = true;
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "  Requesting VC " << out_vc << " at output " << out_port
									<< " (in_pri: " << in_priority << ", out_pri: "
									<< out_priority << ")." << endl;
							watched = true;
						}
						int const input_and_vc =
								_vc_shuffle_requests ?
										(vc * _inputs + input) : (input * _vcs + vc);
						_vc_allocator->AddRequest(input_and_vc, out_port * _vcs + out_vc, 0,
								in_priority, out_priority);
					}
				}
			}
		}
		if (!elig) {
			iter->second.second = STALL_BUFFER_BUSY;
		} else if (_vc_busy_when_full && !cred) {
			iter->second.second =
					reserved ? STALL_BUFFER_RESERVED : STALL_BUFFER_FULL;
		}
	}

	if (watched) {
		*gWatchOut << GetSimTime() << " | " << _vc_allocator->FullName() << " | ";
		_vc_allocator->PrintRequests(gWatchOut);
	}

	_vc_allocator->Allocate();

	if (watched) {
		*gWatchOut << GetSimTime() << " | " << _vc_allocator->FullName() << " | ";
		_vc_allocator->PrintGrants(gWatchOut);
	}

	for (deque<pair<int, pair<pair<int, int>, int> > >::iterator iter =
			_vc_alloc_vcs.begin(); iter != _vc_alloc_vcs.end(); ++iter) {

		int const time = iter->first;
		if (time >= 0) {
			break;
		}
		iter->first = GetSimTime() + _vc_alloc_delay - 1;

		int const input = iter->second.first.first;
		assert((input >= 0) && (input < _inputs));
		int const vc = iter->second.first.second;
		assert((vc >= 0) && (vc < _vcs));

		if (iter->second.second < -1) {
			continue;
		}

		assert(iter->second.second == -1);

		Buffer const * const cur_buf = _buf[input];
		assert(!cur_buf->Empty(vc));
		assert(cur_buf->GetState(vc) == VC::vc_alloc);

		Flit const * const f = cur_buf->FrontFlit(vc);
		assert(f);
		assert(f->vc == vc);
		assert(f->head);

		int const input_and_vc =
				_vc_shuffle_requests ? (vc * _inputs + input) : (input * _vcs + vc);
		int const output_and_vc = _vc_allocator->OutputAssigned(input_and_vc);

		if (output_and_vc >= 0) {

			int const match_output = output_and_vc / _vcs;
			assert((match_output >= 0) && (match_output < _outputs));
			int const match_vc = output_and_vc % _vcs;
			assert((match_vc >= 0) && (match_vc < _vcs));

			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "Assigning VC " << match_vc << " at output " << match_output
						<< " to VC " << vc << " at input " << input << "." << endl;
			}

			iter->second.second = output_and_vc;

		} else {

			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "VC allocation failed for VC " << vc << " at input " << input
						<< "." << endl;
			}

			iter->second.second = STALL_BUFFER_CONFLICT;

		}
	}

	if (_vc_alloc_delay <= 1) {
		return;
	}

	for (deque<pair<int, pair<pair<int, int>, int> > >::iterator iter =
			_vc_alloc_vcs.begin(); iter != _vc_alloc_vcs.end(); ++iter) {

		int const time = iter->first;
		assert(time >= 0);
		if (GetSimTime() < time) {
			break;
		}

		assert(iter->second.second != -1);

		int const output_and_vc = iter->second.second;

		if (output_and_vc >= 0) {

			int const match_output = output_and_vc / _vcs;
			assert((match_output >= 0) && (match_output < _outputs));
			int const match_vc = output_and_vc % _vcs;
			assert((match_vc >= 0) && (match_vc < _vcs));

			BufferState const * const dest_buf = _next_buf[match_output];

			int const input = iter->second.first.first;
			assert((input >= 0) && (input < _inputs));
			int const vc = iter->second.first.second;
			assert((vc >= 0) && (vc < _vcs));

			Buffer const * const cur_buf = _buf[input];
			assert(!cur_buf->Empty(vc));
			assert(cur_buf->GetState(vc) == VC::vc_alloc);

			Flit const * const f = cur_buf->FrontFlit(vc);
			assert(f);
			assert(f->vc == vc);
			assert(f->head);

			if (!dest_buf->IsAvailableFor(match_vc)) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "  Discarding previously generated grant for VC " << vc
							<< " at input " << input << ": VC " << match_vc << " at output "
							<< match_output << " is no longer available." << endl;
				}
				iter->second.second = STALL_BUFFER_BUSY;
			} else if (_vc_busy_when_full && dest_buf->IsFullFor(match_vc)) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "  Discarding previously generated grant for VC " << vc
							<< " at input " << input << ": VC " << match_vc << " at output "
							<< match_output << " has become full." << endl;
				}
				iter->second.second =
						dest_buf->IsFull() ? STALL_BUFFER_FULL : STALL_BUFFER_RESERVED;
			}
		}
	}
}

void IQRouter::_VCAllocUpdate() {
	assert(_vc_allocator);

	while (!_vc_alloc_vcs.empty()) {

		pair<int, pair<pair<int, int>, int> > const & item = _vc_alloc_vcs.front();

		int const time = item.first;
		if ((time < 0) || (GetSimTime() < time)) {
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

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Completed VC allocation for VC " << vc << " at input " << input
					<< " (front: " << f->id << ")." << endl;
		}

		int const output_and_vc = item.second.second;

		if (output_and_vc >= 0) {

			int const match_output = output_and_vc / _vcs;
			assert((match_output >= 0) && (match_output < _outputs));
			int const match_vc = output_and_vc % _vcs;
			assert((match_vc >= 0) && (match_vc < _vcs));

#ifdef POWERGATE // Jiayi
			bool back_to_route = false;
			const FlitChannel * channel = _output_channels[match_output];
			Router * router = channel->GetSink();
			if (router) {
				const bool is_mc = (router->GetID() >= 56);
				if ((_downstream_state[match_output] == draining
						|| _downstream_state[match_output] == wakeup) && !is_mc)
					back_to_route = true;
			}
			if (!back_to_route) {
#endif

				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "  Acquiring assigned VC " << match_vc << " at output "
							<< match_output << "." << endl;
				}

				BufferState * const dest_buf = _next_buf[match_output];
				assert(dest_buf->IsAvailableFor(match_vc));

				dest_buf->TakeBuffer(match_vc, input * _vcs + vc);

				cur_buf->SetOutput(vc, match_output, match_vc);
				cur_buf->SetState(vc, VC::active);
				if (!_speculative) {
					_sw_alloc_vcs.push_back(
							make_pair(-1, make_pair(item.second.first, -1)));
				}
#ifdef POWERGATE // Jiayi
			} else {
				// back to route
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
					*gWatchOut << GetSimTime() << " | " << FullName() << "| "
							<< " Sink router " << router->GetID() << " is "
							<< POWERSTATE[_downstream_state[match_output]]
							<< ", back to RC stage." << endl;
					cur_buf->Display(*gWatchOut);
					*gWatchOut << " route_vcs size: " << _route_vcs.size() << " time: "
							<< _route_vcs.front().first << " input: "
							<< _route_vcs.front().second.first << " vc: "
							<< _route_vcs.front().second.second << " pid: " << f->pid << endl;
					*gWatchOut << " route_vcs empty?"
							<< (_route_vcs.empty() ? " yes" : " no") << endl;
				}
			}
#endif
		} else {
			if (f->watch) {
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

#ifdef POWERGATE

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
			    const bool is_mc = (router->GetID() >= 56);
			    if ((_downstream_state[out_port] == draining
			        || _downstream_state[out_port] == wakeup) && !is_mc)
			      delete_route = true;
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
#endif

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
//			_vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
		}
		_vc_alloc_vcs.pop_front();
	}
}

//------------------------------------------------------------------------------
// switch holding
//------------------------------------------------------------------------------

void IQRouter::_SWHoldEvaluate() {
	assert(_hold_switch_for_packet);

	for (deque<pair<int, pair<pair<int, int>, int> > >::iterator iter =
			_sw_hold_vcs.begin(); iter != _sw_hold_vcs.end(); ++iter) {

		int const time = iter->first;
		if (time >= 0) {
			break;
		}
		iter->first = GetSimTime();

		int const input = iter->second.first.first;
		assert((input >= 0) && (input < _inputs));
		int const vc = iter->second.first.second;
		assert((vc >= 0) && (vc < _vcs));

		assert(iter->second.second == -1);

		Buffer const * const cur_buf = _buf[input];
		assert(!cur_buf->Empty(vc));
		assert(cur_buf->GetState(vc) == VC::active);

		Flit const * const f = cur_buf->FrontFlit(vc);
		assert(f);
		assert(f->vc == vc);

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Beginning held switch allocation for VC " << vc << " at input "
					<< input << " (front: " << f->id << ")." << endl;
		}

		int const expanded_input = input * _input_speedup + vc % _input_speedup;
		assert(_switch_hold_vc[expanded_input] == vc);

		int const match_port = cur_buf->GetOutputPort(vc);
		assert((match_port >= 0) && (match_port < _outputs));
		int const match_vc = cur_buf->GetOutputVC(vc);
		assert((match_vc >= 0) && (match_vc < _vcs));

		int const expanded_output = match_port * _output_speedup
				+ input % _output_speedup;
		assert(_switch_hold_in[expanded_input] == expanded_output);

		BufferState const * const dest_buf = _next_buf[match_port];

		if (dest_buf->IsFullFor(match_vc)) {
			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "  Unable to reuse held connection from input " << input << "."
						<< (expanded_input % _input_speedup) << " to output " << match_port
						<< "." << (expanded_output % _output_speedup)
						<< ": No credit available." << endl;
			}
			iter->second.second =
					dest_buf->IsFull() ? STALL_BUFFER_FULL : STALL_BUFFER_RESERVED;
		} else {
			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "  Reusing held connection from input " << input << "."
						<< (expanded_input % _input_speedup) << " to output " << match_port
						<< "." << (expanded_output % _output_speedup) << "." << endl;
			}
			iter->second.second = expanded_output;
		}
	}
}

void IQRouter::_SWHoldUpdate() {
	assert(_hold_switch_for_packet);

	while (!_sw_hold_vcs.empty()) {

		pair<int, pair<pair<int, int>, int> > const & item = _sw_hold_vcs.front();

		int const time = item.first;
		if (time < 0) {
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

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Completed held switch allocation for VC " << vc << " at input "
					<< input << " (front: " << f->id << ")." << endl;
		}

		int const expanded_input = input * _input_speedup + vc % _input_speedup;
		assert(_switch_hold_vc[expanded_input] == vc);

		int const expanded_output = item.second.second;

		if (expanded_output >= 0
				&& (_output_buffer_size == -1
						|| _output_buffer[expanded_output].size()
								< size_t(_output_buffer_size))) {

			assert(_switch_hold_in[expanded_input] == expanded_output);
			assert(_switch_hold_out[expanded_output] == expanded_input);

			int const output = expanded_output / _output_speedup;
			assert((output >= 0) && (output < _outputs));
			assert(cur_buf->GetOutputPort(vc) == output);

			int const match_vc = cur_buf->GetOutputVC(vc);
			assert((match_vc >= 0) && (match_vc < _vcs));

			BufferState * const dest_buf = _next_buf[output];

//#ifdef POWERGATEi  // Jiayi, useful when with separate speculative SA
//      if (f->head) {  
//        bool back_to_route = false;
//        const FlitChannel * channel = _output_channels[match_output];
//        const Router * router = channel->GetSink();
//        if (router) {
//          const bool is_mc = (router->GetID() >= 56);
////          if (router->GetPowerState() == Router::draining && !is_mc)
//		  	if (_neighbor_state[match_output] == draining && !is_mc)
//            back_to_route = true;
//        }
//        // downstream router is not OFF or myself is ON
//        if (back_to_route && _power_state == Router::power_on) {
//          
//        }
//      }
//#endif

			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "  Scheduling switch connection from input " << input << "."
						<< (vc % _input_speedup) << " to output " << output << "."
						<< (expanded_output % _output_speedup) << "." << endl;
			}

			cur_buf->RemoveFlit(vc);

#ifdef TRACK_FLOWS
			--_stored_flits[f->cl][input];
			if(f->tail) --_active_packets[f->cl][input];
#endif

			_bufferMonitor->read(input, f);

			f->hops++;
			f->vc = match_vc;

			if (!_routing_delay && f->head) {
				const FlitChannel * channel = _output_channels[output];
				const Router * router = channel->GetSink();
				if (router) {
					if (_noq) {
						if (f->watch) {
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
						f->la_route_set.AddRange(next_output_port, next_vc_start,
								next_vc_end);
					} else {
						if (f->watch) {
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

			_crossbar_flits.push_back(
					make_pair(-1,
							make_pair(f, make_pair(expanded_input, expanded_output))));

			if (_out_queue_credits.count(input) == 0) {
				_out_queue_credits.insert(make_pair(input, Credit::New()));
			}
			_out_queue_credits.find(input)->second->vc.insert(vc);

			if (cur_buf->Empty(vc)) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "  Cancelling held connection from input " << input << "."
							<< (expanded_input % _input_speedup) << " to " << output << "."
							<< (expanded_output % _output_speedup) << ": No more flits."
							<< endl;
				}
				_switch_hold_vc[expanded_input] = -1;
				_switch_hold_in[expanded_input] = -1;
				_switch_hold_out[expanded_output] = -1;
				if (f->tail) {
					cur_buf->SetState(vc, VC::idle);
				}
			} else {
				Flit * const nf = cur_buf->FrontFlit(vc);
				assert(nf);
				assert(nf->vc == vc);
				if (f->tail) {
					assert(nf->head);
					if (f->watch) {
						*gWatchOut << GetSimTime() << " | " << FullName() << " | "
								<< "  Cancelling held connection from input " << input << "."
								<< (expanded_input % _input_speedup) << " to " << output << "."
								<< (expanded_output % _output_speedup) << ": End of packet."
								<< endl;
					}
					_switch_hold_vc[expanded_input] = -1;
					_switch_hold_in[expanded_input] = -1;
					_switch_hold_out[expanded_output] = -1;
					if (_routing_delay) {
						cur_buf->SetState(vc, VC::routing);
						_route_vcs.push_back(make_pair(-1, item.second.first));
					} else {
						if (nf->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Using precomputed lookahead routing information for VC "
									<< vc << " at input " << input << " (front: " << nf->id
									<< ")." << endl;
						}
						cur_buf->SetRouteSet(vc, &nf->la_route_set);
						cur_buf->SetState(vc, VC::vc_alloc);
						if (_speculative) {
							_sw_alloc_vcs.push_back(
									make_pair(-1, make_pair(item.second.first, -1)));
						}
						if (_vc_allocator) {
							_vc_alloc_vcs.push_back(
									make_pair(-1, make_pair(item.second.first, -1)));
						}
						if (_noq) {
							_UpdateNOQ(input, vc, nf);
						}
					}
				} else {
					_sw_hold_vcs.push_back(
							make_pair(-1, make_pair(item.second.first, -1)));
				}
			}
		} else {
			//when internal speedup >1.0, the buffer stall stats may not be accruate
			assert(
					(expanded_output == STALL_BUFFER_FULL)
							|| (expanded_output == STALL_BUFFER_RESERVED)
							|| !(_output_buffer_size == -1
									|| _output_buffer[expanded_output].size()
											< size_t(_output_buffer_size)));

			int const held_expanded_output = _switch_hold_in[expanded_input];
			assert(held_expanded_output >= 0);

			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "  Cancelling held connection from input " << input << "."
						<< (expanded_input % _input_speedup) << " to "
						<< (held_expanded_output / _output_speedup) << "."
						<< (held_expanded_output % _output_speedup) << ": Flit not sent."
						<< endl;
			}
			_switch_hold_vc[expanded_input] = -1;
			_switch_hold_in[expanded_input] = -1;
			_switch_hold_out[held_expanded_output] = -1;
			_sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
		}
		_sw_hold_vcs.pop_front();
	}
}

//------------------------------------------------------------------------------
// switch allocation
//------------------------------------------------------------------------------

bool IQRouter::_SWAllocAddReq(int input, int vc, int output) {
	assert(input >= 0 && input < _inputs);
	assert(vc >= 0 && vc < _vcs);
	assert(output >= 0 && output < _outputs);

	// When input_speedup > 1, the virtual channel buffers are interleaved to
	// create multiple input ports to the switch. Similarily, the output ports
	// are interleaved based on their originating input when output_speedup > 1.

	int const expanded_input = input * _input_speedup + vc % _input_speedup;
	int const expanded_output = output * _output_speedup
			+ input % _output_speedup;

	Buffer const * const cur_buf = _buf[input];
	assert(!cur_buf->Empty(vc));
	assert(
			(cur_buf->GetState(vc) == VC::active)
					|| (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));

	Flit const * const f = cur_buf->FrontFlit(vc);
	assert(f);
	assert(f->vc == vc);

	if ((_switch_hold_in[expanded_input] < 0)
			&& (_switch_hold_out[expanded_output] < 0)) {

		Allocator * allocator = _sw_allocator;
		int prio = cur_buf->GetPriority(vc);

		if (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)) {
			if (_spec_sw_allocator) {
				allocator = _spec_sw_allocator;
			} else {
				assert(prio >= 0);
				prio += numeric_limits<int>::min();
			}
		}

		Allocator::sRequest req;

		if (allocator->ReadRequest(req, expanded_input, expanded_output)) {
			if (RoundRobinArbiter::Supersedes(vc, prio, req.label, req.in_pri,
					_sw_rr_offset[expanded_input], _vcs)) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "  Replacing earlier request from VC " << req.label
							<< " for output " << output << "."
							<< (expanded_output % _output_speedup) << " with priority "
							<< req.in_pri << " ("
							<< ((cur_buf->GetState(vc) == VC::active) ? "non-spec" : "spec")
							<< ", pri: " << prio << ")." << endl;
				}
				allocator->RemoveRequest(expanded_input, expanded_output, req.label);
				allocator->AddRequest(expanded_input, expanded_output, vc, prio, prio);
				return true;
			}
			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "  Output " << output << "."
						<< (expanded_output % _output_speedup)
						<< " was already requested by VC " << req.label << " with priority "
						<< req.in_pri << " (pri: " << prio << ")." << endl;
			}
			return false;
		}
		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "  Requesting output " << output << "."
					<< (expanded_output % _output_speedup) << " ("
					<< ((cur_buf->GetState(vc) == VC::active) ? "non-spec" : "spec")
					<< ", pri: " << prio << ")." << endl;
		}
		allocator->AddRequest(expanded_input, expanded_output, vc, prio, prio);
		return true;
	}
	if (f->watch) {
		*gWatchOut << GetSimTime() << " | " << FullName() << " | "
				<< "  Ignoring output " << output << "."
				<< (expanded_output % _output_speedup) << " due to switch hold (";
		if (_switch_hold_in[expanded_input] >= 0) {
			*gWatchOut << "input: " << input << "."
					<< (expanded_input % _input_speedup);
			if (_switch_hold_out[expanded_output] >= 0) {
				*gWatchOut << ", ";
			}
		}
		if (_switch_hold_out[expanded_output] >= 0) {
			*gWatchOut << "output: " << output << "."
					<< (expanded_output % _output_speedup);
		}
		*gWatchOut << ")." << endl;
	}
	return false;
}

void IQRouter::_SWAllocEvaluate() {
	bool watched = false;

	for (deque<pair<int, pair<pair<int, int>, int> > >::iterator iter =
			_sw_alloc_vcs.begin(); iter != _sw_alloc_vcs.end(); ++iter) {

		int const time = iter->first;
		if (time >= 0) {
			break;
		}

		int const input = iter->second.first.first;
		assert((input >= 0) && (input < _inputs));
		int const vc = iter->second.first.second;
		assert((vc >= 0) && (vc < _vcs));

		assert(iter->second.second == -1);

		assert(_switch_hold_vc[input * _input_speedup + vc % _input_speedup] != vc);

		Buffer const * const cur_buf = _buf[input];
		assert(!cur_buf->Empty(vc));
		assert(
				(cur_buf->GetState(vc) == VC::active)
						|| (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));

		Flit const * const f = cur_buf->FrontFlit(vc);
		assert(f);
		assert(f->vc == vc);

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Beginning switch allocation for VC " << vc << " at input "
					<< input << " (front: " << f->id << ")." << endl;
		}

		if (cur_buf->GetState(vc) == VC::active) {

			int const dest_output = cur_buf->GetOutputPort(vc);
			assert((dest_output >= 0) && (dest_output < _outputs));
			int const dest_vc = cur_buf->GetOutputVC(vc);
			assert((dest_vc >= 0) && (dest_vc < _vcs));

			BufferState const * const dest_buf = _next_buf[dest_output];

			if (dest_buf->IsFullFor(dest_vc)
					|| (_output_buffer_size != -1
							&& _output_buffer[dest_output].size()
									>= (size_t) (_output_buffer_size))) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | " << "  VC "
							<< dest_vc << " at output " << dest_output << " is full." << endl;
				}
				iter->second.second =
						dest_buf->IsFull() ? STALL_BUFFER_FULL : STALL_BUFFER_RESERVED;
				continue;
			}
			bool const requested = _SWAllocAddReq(input, vc, dest_output);
			watched |= requested && f->watch;
			continue;
		}
		assert(_speculative && (cur_buf->GetState(vc) == VC::vc_alloc));
		assert(f->head);

		// The following models the speculative VC allocation aspects of the
		// pipeline. An input VC with a request in for an egress virtual channel
		// will also speculatively bid for the switch regardless of whether the VC
		// allocation succeeds.

		OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
		assert(route_set);

		set<OutputSet::sSetElement> const setlist = route_set->GetSet();

		assert(!_noq || (setlist.size() == 1));

		for (set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
				iset != setlist.end(); ++iset) {

			int const dest_output = iset->output_port;
			assert((dest_output >= 0) && (dest_output < _outputs));

			// for lower levels of speculation, ignore credit availability and always
			// issue requests for all output ports in route set

			BufferState const * const dest_buf = _next_buf[dest_output];

			bool elig = false;
			bool cred = false;

			if (_spec_check_elig) {

				// for higher levels of speculation, check if at least one suitable VC
				// is available at the current output

				int vc_start;
				int vc_end;

				if (_noq && _noq_next_output_port[input][vc] >= 0) {
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

				for (int dest_vc = vc_start; dest_vc <= vc_end; ++dest_vc) {
					assert((dest_vc >= 0) && (dest_vc < _vcs));

					if (dest_buf->IsAvailableFor(dest_vc)
							&& (_output_buffer_size == -1
									|| _output_buffer[dest_output].size()
											< (size_t) (_output_buffer_size))) {
						elig = true;
						if (!_spec_check_cred || !dest_buf->IsFullFor(dest_vc)) {
							cred = true;
							break;
						}
					}
				}
			}

			if (_spec_check_elig && !elig) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "  Output " << dest_output << " has no suitable VCs available."
							<< endl;
				}
				iter->second.second = STALL_BUFFER_BUSY;
			} else if (_spec_check_cred && !cred) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "  All suitable VCs at output " << dest_output << " are full."
							<< endl;
				}
				iter->second.second =
						dest_buf->IsFull() ? STALL_BUFFER_FULL : STALL_BUFFER_RESERVED;
			} else {
				bool const requested = _SWAllocAddReq(input, vc, dest_output);
				watched |= requested && f->watch;
			}
		}
	}

	if (watched) {
		*gWatchOut << GetSimTime() << " | " << _sw_allocator->FullName() << " | ";
		_sw_allocator->PrintRequests(gWatchOut);
		if (_spec_sw_allocator) {
			*gWatchOut << GetSimTime() << " | " << _spec_sw_allocator->FullName()
					<< " | ";
			_spec_sw_allocator->PrintRequests(gWatchOut);
		}
	}

	_sw_allocator->Allocate();
	if (_spec_sw_allocator)
		_spec_sw_allocator->Allocate();

	if (watched) {
		*gWatchOut << GetSimTime() << " | " << _sw_allocator->FullName() << " | ";
		_sw_allocator->PrintGrants(gWatchOut);
		if (_spec_sw_allocator) {
			*gWatchOut << GetSimTime() << " | " << _spec_sw_allocator->FullName()
					<< " | ";
			_spec_sw_allocator->PrintGrants(gWatchOut);
		}
	}

	for (deque<pair<int, pair<pair<int, int>, int> > >::iterator iter =
			_sw_alloc_vcs.begin(); iter != _sw_alloc_vcs.end(); ++iter) {

		int const time = iter->first;
		if (time >= 0) {
			break;
		}
		iter->first = GetSimTime() + _sw_alloc_delay - 1;

		int const input = iter->second.first.first;
		assert((input >= 0) && (input < _inputs));
		int const vc = iter->second.first.second;
		assert((vc >= 0) && (vc < _vcs));

		if (iter->second.second < -1) {
			continue;
		}

		assert(iter->second.second == -1);

		Buffer const * const cur_buf = _buf[input];
		assert(!cur_buf->Empty(vc));
		assert(
				(cur_buf->GetState(vc) == VC::active)
						|| (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));

		Flit const * const f = cur_buf->FrontFlit(vc);
		assert(f);
		assert(f->vc == vc);

		int const expanded_input = input * _input_speedup + vc % _input_speedup;

		int expanded_output = _sw_allocator->OutputAssigned(expanded_input);

		if (expanded_output >= 0) {
			assert((expanded_output % _output_speedup) == (input % _output_speedup));
			int const granted_vc = _sw_allocator->ReadRequest(expanded_input,
					expanded_output);
			if (granted_vc == vc) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "Assigning output " << (expanded_output / _output_speedup)
							<< "." << (expanded_output % _output_speedup) << " to VC " << vc
							<< " at input " << input << "." << (vc % _input_speedup) << "."
							<< endl;
				}
				_sw_rr_offset[expanded_input] = (vc + _input_speedup) % _vcs;
				iter->second.second = expanded_output;
			} else {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "Switch allocation failed for VC " << vc << " at input "
							<< input << ": Granted to VC " << granted_vc << "." << endl;
				}
				iter->second.second = STALL_CROSSBAR_CONFLICT;
			}
		} else if (_spec_sw_allocator) {
			expanded_output = _spec_sw_allocator->OutputAssigned(expanded_input);
			if (expanded_output >= 0) {
				assert(
						(expanded_output % _output_speedup) == (input % _output_speedup));
				if (_spec_mask_by_reqs
						&& _sw_allocator->OutputHasRequests(expanded_output)) {
					if (f->watch) {
						*gWatchOut << GetSimTime() << " | " << FullName() << " | "
								<< "Discarding speculative grant for VC " << vc << " at input "
								<< input << "." << (vc % _input_speedup) << " because output "
								<< (expanded_output / _output_speedup) << "."
								<< (expanded_output % _output_speedup)
								<< " has non-speculative requests." << endl;
					}
					iter->second.second = STALL_CROSSBAR_CONFLICT;
				} else if (!_spec_mask_by_reqs
						&& (_sw_allocator->InputAssigned(expanded_output) >= 0)) {
					if (f->watch) {
						*gWatchOut << GetSimTime() << " | " << FullName() << " | "
								<< "Discarding speculative grant for VC " << vc << " at input "
								<< input << "." << (vc % _input_speedup) << " because output "
								<< (expanded_output / _output_speedup) << "."
								<< (expanded_output % _output_speedup)
								<< " has a non-speculative grant." << endl;
					}
					iter->second.second = STALL_CROSSBAR_CONFLICT;
				} else {
					int const granted_vc = _spec_sw_allocator->ReadRequest(expanded_input,
							expanded_output);
					if (granted_vc == vc) {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Assigning output " << (expanded_output / _output_speedup)
									<< "." << (expanded_output % _output_speedup) << " to VC "
									<< vc << " at input " << input << "." << (vc % _input_speedup)
									<< "." << endl;
						}
						_sw_rr_offset[expanded_input] = (vc + _input_speedup) % _vcs;
						iter->second.second = expanded_output;
					} else {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Switch allocation failed for VC " << vc << " at input "
									<< input << ": Granted to VC " << granted_vc << "." << endl;
						}
						iter->second.second = STALL_CROSSBAR_CONFLICT;
					}
				}
			} else {

				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "Switch allocation failed for VC " << vc << " at input "
							<< input << ": No output granted." << endl;
				}

				iter->second.second = STALL_CROSSBAR_CONFLICT;

			}
		} else {

			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "Switch allocation failed for VC " << vc << " at input " << input
						<< ": No output granted." << endl;
			}

			iter->second.second = STALL_CROSSBAR_CONFLICT;

		}
	}

	if (!_speculative && (_sw_alloc_delay <= 1)) {
		return;
	}

	for (deque<pair<int, pair<pair<int, int>, int> > >::iterator iter =
			_sw_alloc_vcs.begin(); iter != _sw_alloc_vcs.end(); ++iter) {

		int const time = iter->first;
		assert(time >= 0);
		if (GetSimTime() < time) {
			break;
		}

		assert(iter->second.second != -1);

		int const expanded_output = iter->second.second;

		if (expanded_output >= 0) {

			int const output = expanded_output / _output_speedup;
			assert((output >= 0) && (output < _outputs));

			BufferState const * const dest_buf = _next_buf[output];

			int const input = iter->second.first.first;
			assert((input >= 0) && (input < _inputs));
			assert((input % _output_speedup) == (expanded_output % _output_speedup));
			int const vc = iter->second.first.second;
			assert((vc >= 0) && (vc < _vcs));

			int const expanded_input = input * _input_speedup + vc % _input_speedup;
			assert(_switch_hold_vc[expanded_input] != vc);

			Buffer const * const cur_buf = _buf[input];
			assert(!cur_buf->Empty(vc));
			assert(
					(cur_buf->GetState(vc) == VC::active)
							|| (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));

			Flit const * const f = cur_buf->FrontFlit(vc);
			assert(f);
			assert(f->vc == vc);

			if ((_switch_hold_in[expanded_input] >= 0)
					|| (_switch_hold_out[expanded_output] >= 0)) {
				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "Discarding grant from input " << input << "."
							<< (vc % _input_speedup) << " to output " << output << "."
							<< (expanded_output % _output_speedup)
							<< " due to conflict with held connection at ";
					if (_switch_hold_in[expanded_input] >= 0) {
						*gWatchOut << "input";
					}
					if ((_switch_hold_in[expanded_input] >= 0)
							&& (_switch_hold_out[expanded_output] >= 0)) {
						*gWatchOut << " and ";
					}
					if (_switch_hold_out[expanded_output] >= 0) {
						*gWatchOut << "output";
					}
					*gWatchOut << "." << endl;
				}
				iter->second.second = STALL_CROSSBAR_CONFLICT;
			} else if (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)) {

				assert(f->head);

				if (_vc_allocator) { // separate VC and switch allocators

					int const input_and_vc =
							_vc_shuffle_requests ?
									(vc * _inputs + input) : (input * _vcs + vc);
					int const output_and_vc = _vc_allocator->OutputAssigned(input_and_vc);

					if (output_and_vc < 0) {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Discarding grant from input " << input << "."
									<< (vc % _input_speedup) << " to output " << output << "."
									<< (expanded_output % _output_speedup)
									<< " due to misspeculation." << endl;
						}
						iter->second.second = -1; // stall is counted in VC allocation path!
					} else if ((output_and_vc / _vcs) != output) {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Discarding grant from input " << input << "."
									<< (vc % _input_speedup) << " to output " << output << "."
									<< (expanded_output % _output_speedup)
									<< " due to port mismatch between VC and switch allocator."
									<< endl;
						}
						iter->second.second = STALL_BUFFER_CONFLICT; // count this case as if we had failed allocation
					} else if (dest_buf->IsFullFor((output_and_vc % _vcs))) {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Discarding grant from input " << input << "."
									<< (vc % _input_speedup) << " to output " << output << "."
									<< (expanded_output % _output_speedup)
									<< " due to lack of credit." << endl;
						}
						iter->second.second =
								dest_buf->IsFull() ? STALL_BUFFER_FULL : STALL_BUFFER_RESERVED;
					}

				} else { // VC allocation is piggybacked onto switch allocation

					OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
					assert(route_set);

					set<OutputSet::sSetElement> const setlist = route_set->GetSet();

					bool busy = true;
					bool full = true;
					bool reserved = false;

					assert(!_noq || (setlist.size() == 1));

					for (set<OutputSet::sSetElement>::const_iterator iset =
							setlist.begin(); iset != setlist.end(); ++iset) {
						if (iset->output_port == output) {

							int vc_start;
							int vc_end;

							if (_noq && _noq_next_output_port[input][vc] >= 0) {
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

							for (int out_vc = vc_start; out_vc <= vc_end; ++out_vc) {
								assert((out_vc >= 0) && (out_vc < _vcs));
								if (dest_buf->IsAvailableFor(out_vc)) {
									busy = false;
									if (!dest_buf->IsFullFor(out_vc)) {
										full = false;
										break;
									} else if (!dest_buf->IsFull()) {
										reserved = true;
									}
								}
							}
							if (!full) {
								break;
							}
						}
					}

					if (busy) {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Discarding grant from input " << input << "."
									<< (vc % _input_speedup) << " to output " << output << "."
									<< (expanded_output % _output_speedup)
									<< " because no suitable output VC for piggyback allocation is available."
									<< endl;
						}
						iter->second.second = STALL_BUFFER_BUSY;
					} else if (full) {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Discarding grant from input " << input << "."
									<< (vc % _input_speedup) << " to output " << output << "."
									<< (expanded_output % _output_speedup)
									<< " because all suitable output VCs for piggyback allocation are full."
									<< endl;
						}
						iter->second.second =
								reserved ? STALL_BUFFER_RESERVED : STALL_BUFFER_FULL;
					}

				}

			} else {
				assert(cur_buf->GetOutputPort(vc) == output);

				int const match_vc = cur_buf->GetOutputVC(vc);
				assert((match_vc >= 0) && (match_vc < _vcs));

				if (dest_buf->IsFullFor(match_vc)) {
					if (f->watch) {
						*gWatchOut << GetSimTime() << " | " << FullName() << " | "
								<< "  Discarding grant from input " << input << "."
								<< (vc % _input_speedup) << " to output " << output << "."
								<< (expanded_output % _output_speedup)
								<< " due to lack of credit." << endl;
					}
					iter->second.second =
							dest_buf->IsFull() ? STALL_BUFFER_FULL : STALL_BUFFER_RESERVED;
				}
			}
		}
	}
}

void IQRouter::_SWAllocUpdate() {
	while (!_sw_alloc_vcs.empty()) {

		pair<int, pair<pair<int, int>, int> > const & item = _sw_alloc_vcs.front();

		int const time = item.first;
		if ((time < 0) || (GetSimTime() < time)) {
			break;
		}
		assert(GetSimTime() == time);

		int const input = item.second.first.first;
		assert((input >= 0) && (input < _inputs));
		int const vc = item.second.first.second;
		assert((vc >= 0) && (vc < _vcs));

		Buffer * const cur_buf = _buf[input];
		assert(!cur_buf->Empty(vc));
		assert(
				(cur_buf->GetState(vc) == VC::active)
						|| (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));

		Flit * const f = cur_buf->FrontFlit(vc);
		assert(f);
		assert(f->vc == vc);

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Completed switch allocation for VC " << vc << " at input "
					<< input << " (front: " << f->id << ")." << endl;
		}

		int const expanded_output = item.second.second;

		if (expanded_output >= 0) {

			int const expanded_input = input * _input_speedup + vc % _input_speedup;
			assert(_switch_hold_vc[expanded_input] < 0);
			assert(_switch_hold_in[expanded_input] < 0);
			assert(_switch_hold_out[expanded_output] < 0);

			int const output = expanded_output / _output_speedup;
			assert((output >= 0) && (output < _outputs));

			BufferState * const dest_buf = _next_buf[output];

			int match_vc;

			if (!_vc_allocator && (cur_buf->GetState(vc) == VC::vc_alloc)) {

				assert(f->head);

				int const cl = f->cl;
				assert((cl >= 0) && (cl < _classes));

				int const vc_offset = _vc_rr_offset[output * _classes + cl];

				match_vc = -1;
				int match_prio = numeric_limits<int>::min();

				const OutputSet * route_set = cur_buf->GetRouteSet(vc);
				set<OutputSet::sSetElement> const setlist = route_set->GetSet();

				assert(!_noq || (setlist.size() == 1));

				for (set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
						iset != setlist.end(); ++iset) {
					if (iset->output_port == output) {

						int vc_start;
						int vc_end;

						if (_noq && _noq_next_output_port[input][vc] >= 0) {
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

						for (int out_vc = vc_start; out_vc <= vc_end; ++out_vc) {
							assert((out_vc >= 0) && (out_vc < _vcs));

							int vc_prio = iset->pri;
							if (_vc_prioritize_empty && !dest_buf->IsEmptyFor(out_vc)) {
								assert(vc_prio >= 0);
								vc_prio += numeric_limits<int>::min();
							}

							// FIXME: This check should probably be performed in Evaluate(),
							// not Update(), as the latter can cause the outcome to depend on
							// the order of evaluation!
							if (dest_buf->IsAvailableFor(out_vc)
									&& !dest_buf->IsFullFor(out_vc)
									&& ((match_vc < 0)
											|| RoundRobinArbiter::Supersedes(out_vc, vc_prio,
													match_vc, match_prio, vc_offset, _vcs))) {
								match_vc = out_vc;
								match_prio = vc_prio;
							}
						}
					}
				}
				assert(match_vc >= 0);

				if (f->watch) {
					*gWatchOut << GetSimTime() << " | " << FullName() << " | "
							<< "  Allocating VC " << match_vc << " at output " << output
							<< " via piggyback VC allocation." << endl;
				}

				cur_buf->SetState(vc, VC::active);
				cur_buf->SetOutput(vc, output, match_vc);
				dest_buf->TakeBuffer(match_vc, input * _vcs + vc);

				_vc_rr_offset[output * _classes + cl] = (match_vc + 1) % _vcs;

			} else {

				assert(cur_buf->GetOutputPort(vc) == output);

				match_vc = cur_buf->GetOutputVC(vc);

#ifdef POWERGATE // Jiayi, in SA, push back to RC if downstream router is draining
				if (f->head) {
					bool back_to_route = false;
					const FlitChannel * channel = _output_channels[output];
					Router * router = channel->GetSink();
					if (router) {
						const bool is_mc = (router->GetID() >= 56);
						if ((_downstream_state[output] == draining
								|| _downstream_state[output] == wakeup) && !is_mc) {
							back_to_route = true;
						}
					}
					// downstream is not power on
					if (back_to_route) {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< " SA: Sink router " << router->GetID() << " is "
									<< POWERSTATE[_downstream_state[output]]
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
						  assert(0); // should not come here
						  dest_buf->ReturnBuffer(match_vc);
						}
						cur_buf->ClearRouteSet(vc);
						cur_buf->SetState(vc, VC::routing);
						_route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
						_sw_alloc_vcs.pop_front();
						continue;
					}
				}
#endif
			}
			assert((match_vc >= 0) && (match_vc < _vcs));

			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "  Scheduling switch connection from input " << input << "."
						<< (vc % _input_speedup) << " to output " << output << "."
						<< (expanded_output % _output_speedup) << "." << endl;
			}

			cur_buf->RemoveFlit(vc);

#ifdef TRACK_FLOWS
			--_stored_flits[f->cl][input];
			if(f->tail) --_active_packets[f->cl][input];
#endif

			_bufferMonitor->read(input, f);

			f->hops++;
			f->vc = match_vc;

			if (!_routing_delay && f->head) {
				const FlitChannel * channel = _output_channels[output];
				const Router * router = channel->GetSink();
				if (router) {
					if (_noq) {
						if (f->watch) {
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
						f->la_route_set.AddRange(next_output_port, next_vc_start,
								next_vc_end);
					} else {
						if (f->watch) {
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

			_crossbar_flits.push_back(
					make_pair(-1,
							make_pair(f, make_pair(expanded_input, expanded_output))));

			if (_out_queue_credits.count(input) == 0) {
				_out_queue_credits.insert(make_pair(input, Credit::New()));
			}
			_out_queue_credits.find(input)->second->vc.insert(vc);

			if (cur_buf->Empty(vc)) {
				if (f->tail) {
					cur_buf->SetState(vc, VC::idle);
				}
			} else {
				Flit * const nf = cur_buf->FrontFlit(vc);
				assert(nf);
				assert(nf->vc == vc);
				if (f->tail) {
					assert(nf->head);
					if (_routing_delay) {
						cur_buf->SetState(vc, VC::routing);
						_route_vcs.push_back(make_pair(-1, item.second.first));
					} else {
						if (nf->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Using precomputed lookahead routing information for VC "
									<< vc << " at input " << input << " (front: " << nf->id
									<< ")." << endl;
						}
						cur_buf->SetRouteSet(vc, &nf->la_route_set);
						cur_buf->SetState(vc, VC::vc_alloc);
						if (_speculative) {
							_sw_alloc_vcs.push_back(
									make_pair(-1, make_pair(item.second.first, -1)));
						}
						if (_vc_allocator) {
							_vc_alloc_vcs.push_back(
									make_pair(-1, make_pair(item.second.first, -1)));
						}
						if (_noq) {
							_UpdateNOQ(input, vc, nf);
						}
					}
				} else {
					if (_hold_switch_for_packet) {
						if (f->watch) {
							*gWatchOut << GetSimTime() << " | " << FullName() << " | "
									<< "Setting up switch hold for VC " << vc << " at input "
									<< input << "." << (expanded_input % _input_speedup)
									<< " to output " << output << "."
									<< (expanded_output % _output_speedup) << "." << endl;
						}
						_switch_hold_vc[expanded_input] = vc;
						_switch_hold_in[expanded_input] = expanded_output;
						_switch_hold_out[expanded_output] = expanded_input;
						_sw_hold_vcs.push_back(
								make_pair(-1, make_pair(item.second.first, -1)));
					} else {
						_sw_alloc_vcs.push_back(
								make_pair(-1, make_pair(item.second.first, -1)));
					}
				}
			}
		} else {

#ifdef POWERGATE // Jiayi, in SA, push back to RC if downstream router is draining


			if (f->head) {

			  bool back_to_route = false;

			  int output = cur_buf->GetOutputPort(vc);
			  int match_vc = cur_buf->GetOutputVC(vc);

			  if (cur_buf->GetState(vc) == VC::vc_alloc) { // miss-speculation

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
			        const bool is_mc = (router->GetID() >= 56);
			        if ((_downstream_state[out_port] == draining
			            || _downstream_state[out_port] == wakeup) && !is_mc)
			          delete_route = true;
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
			      const bool is_mc = (router->GetID() >= 56);
			      if ((_downstream_state[output] == draining
			          || _downstream_state[output] == wakeup) && !is_mc) {
			        back_to_route = true;
			      }
			    }
			    if (back_to_route) {
			    	BufferState * dest_buf = _next_buf[output];
			    	dest_buf->ReturnBuffer(match_vc);
			    }
			  }


				// downstream is not power on
				if (back_to_route) {
//					if (f->watch) {
//						*gWatchOut << GetSimTime() << " | " << FullName() << " | "
//								<< " SA: Sink router " << router->GetID() << " is "
//								<< POWERSTATE[_downstream_state[output]]
//								<< ", back to RC stage." << endl;
//					}
					cur_buf->ClearRouteSet(vc);
					cur_buf->SetState(vc, VC::routing);
					_route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
					_sw_alloc_vcs.pop_front();
					continue;
				}
			}
#endif
			if (f->watch) {
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "  No output port allocated." << endl;
			}

#ifdef TRACK_STALLS
			assert((expanded_output == -1) ||
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

			if (GetSimTime() - f->rtime == 300 && f->head) { // timeout
			  int const input = item.second.first.first;
			  assert((input >= 0) && (input < _inputs));
			  int const vc = item.second.first.second;
			  assert((vc >= 0) && (vc < _vcs));
			  Buffer * const cur_buf = _buf[input];
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

//			_sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
		}
		_sw_alloc_vcs.pop_front();
	}
}

//------------------------------------------------------------------------------
// switch traversal
//------------------------------------------------------------------------------

void IQRouter::_SwitchEvaluate() {
	for (deque<pair<int, pair<Flit *, pair<int, int> > > >::iterator iter =
			_crossbar_flits.begin(); iter != _crossbar_flits.end(); ++iter) {

		int const time = iter->first;
		if (time >= 0) {
			break;
		}
		iter->first = GetSimTime() + _crossbar_delay - 1;

		Flit const * const f = iter->second.first;
		assert(f);

		int const expanded_input = iter->second.second.first;
		int const expanded_output = iter->second.second.second;

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Beginning crossbar traversal for flit " << f->id << " from input "
					<< (expanded_input / _input_speedup) << "."
					<< (expanded_input % _input_speedup) << " to output "
					<< (expanded_output / _output_speedup) << "."
					<< (expanded_output % _output_speedup) << "." << endl;
		}
	}
}

void IQRouter::_SwitchUpdate() {
	while (!_crossbar_flits.empty()) {

		pair<int, pair<Flit *, pair<int, int> > > const & item =
				_crossbar_flits.front();

		int const time = item.first;
		if ((time < 0) || (GetSimTime() < time)) {
			break;
		}
		assert(GetSimTime() == time);

		Flit * const f = item.second.first;
		assert(f);

		int const expanded_input = item.second.second.first;
		int const input = expanded_input / _input_speedup;
		assert((input >= 0) && (input < _inputs));
		int const expanded_output = item.second.second.second;
		int const output = expanded_output / _output_speedup;
		assert((output >= 0) && (output < _outputs));

//#ifdef POWERGATE
//		const FlitChannel * channel = _output_channels[output];
//		Router * router = channel->GetSink();
//		if (router) {
////			assert(router->GetPowerState() == power_on || router->GetPowerState() == draining);
//			assert(_neighbor_state[output] == power_on || _neighbor_state[output] == draining);
////			if (router->GetPowerState() == draining && f->head) {
//			if (_neighbor_state[output] == draining && f->head) {
//				router->WakeUp();
//			}
//		}
//#endif

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Completed crossbar traversal for flit " << f->id << " from input "
					<< input << "." << (expanded_input % _input_speedup) << " to output "
					<< output << "." << (expanded_output % _output_speedup) << "."
					<< endl;
		}
		_switchMonitor->traversal(input, output, f);

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Buffering flit " << f->id << " at output " << output << "."
					<< endl;
		}
		_output_buffer[output].push(f);
		//the output buffer size isn't precise due to flits in flight
		//but there is a maximum bound based on output speed up and ST traversal
		assert(
				_output_buffer[output].size()
						<= (size_t )_output_buffer_size + _crossbar_delay * _output_speedup
								+ (_output_speedup - 1) || _output_buffer_size == -1);
		_crossbar_flits.pop_front();
	}
}

//------------------------------------------------------------------------------
// output queuing
//------------------------------------------------------------------------------

void IQRouter::_OutputQueuing() {
	for (map<int, Credit *>::const_iterator iter = _out_queue_credits.begin();
			iter != _out_queue_credits.end(); ++iter) {

		int const input = iter->first;
		assert((input >= 0) && (input < _inputs));

		Credit * const c = iter->second;
		assert(c);
		assert(!c->vc.empty());

		_credit_buffer[input].push(c);
	}
	_out_queue_credits.clear();

	for (map<int, Handshake *>::const_iterator iter =
			_out_queue_handshakes.begin(); iter != _out_queue_handshakes.end();
			++iter) {

		int const output = iter->first;
		assert((output >= 0) && (output < _outputs - 1));

		Handshake * const h = iter->second;
		assert(h);
		assert((h->new_state >= 0 || h->drain_done || h->wakeup) && h->id >= 0);

#ifdef DEBUG_POWERGATE
		if ((_id == 31 && output == 3)) {// || (_id == 24 && output == 0)) {
			cout << GetSimTime() << " | " << "router#" << _id << " send handshake to output "
					<< output << ", src id: " << h->id << ",new state: " << h->new_state
					<< ", src state: " << h->src_state << ", drain tag: " << h->drain_done
					<< ", downstream state: " << POWERSTATE[_downstream_state[output]] << endl;
		}
#endif

		_handshake_buffer[output].push(h);
	}
	_out_queue_handshakes.clear();
}

//------------------------------------------------------------------------------
// write outputs
//------------------------------------------------------------------------------

void IQRouter::_SendFlits() {
	for (int output = 0; output < _outputs; ++output) {
		if (!_output_buffer[output].empty()) {
			Flit * const f = _output_buffer[output].front();
			assert(f);
			_output_buffer[output].pop();

#ifdef TRACK_FLOWS
			++_sent_flits[f->cl][output];
#endif

			if (f->watch)
				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
						<< "Sending flit " << f->id << " to channel at output " << output
						<< "." << endl;
			if (gTrace) {
				cout << "Outport " << output << endl << "Stop Mark" << endl;
			}
			_output_channels[output]->Send(f);
		}
	}
}

void IQRouter::_SendCredits() {
	for (int input = 0; input < _inputs; ++input) {
		if (!_credit_buffer[input].empty()) {
			Credit * const c = _credit_buffer[input].front();
			assert(c);
			_credit_buffer[input].pop();
			_input_credits[input]->Send(c);
		}
	}
}

void IQRouter::_SendHandshakes() {	// Jiayi
	for (int output = 0; output < _outputs - 1; ++output) {
		if (!_handshake_buffer[output].empty()) {
			Handshake * const h = _handshake_buffer[output].front();
			assert(h);
			_handshake_buffer[output].pop();
			_output_handshakes[output]->Send(h);
		}
	}
}

//------------------------------------------------------------------------------
// misc.
//------------------------------------------------------------------------------

void IQRouter::Display(ostream & os) const {
	for (int input = 0; input < _inputs; ++input) {
		_buf[input]->Display(os);
		//_next_buf[input]->Display(os);
	}
}

int IQRouter::GetUsedCredit(int o) const {
	assert((o >= 0) && (o < _outputs));
	BufferState const * const dest_buf = _next_buf[o];
	return dest_buf->Occupancy();
}

int IQRouter::GetBufferOccupancy(int i) const {
	assert(i >= 0 && i < _inputs);
	return _buf[i]->GetOccupancy();
}

#ifdef TRACK_BUFFERS
int IQRouter::GetUsedCreditForClass(int output, int cl) const
{
	assert((output >= 0) && (output < _outputs));
	BufferState const * const dest_buf = _next_buf[output];
	return dest_buf->OccupancyForClass(cl);
}

int IQRouter::GetBufferOccupancyForClass(int input, int cl) const
{
	assert((input >= 0) && (input < _inputs));
	return _buf[input]->GetOccupancyForClass(cl);
}
#endif

vector<int> IQRouter::UsedCredits() const {
	vector<int> result(_outputs * _vcs);
	for (int o = 0; o < _outputs; ++o) {
		for (int v = 0; v < _vcs; ++v) {
			result[o * _vcs + v] = _next_buf[o]->OccupancyFor(v);
		}
	}
	return result;
}

vector<int> IQRouter::FreeCredits() const {
	vector<int> result(_outputs * _vcs);
	for (int o = 0; o < _outputs; ++o) {
		for (int v = 0; v < _vcs; ++v) {
			result[o * _vcs + v] = _next_buf[o]->AvailableFor(v);
		}
	}
	return result;
}

vector<int> IQRouter::MaxCredits() const {
	vector<int> result(_outputs * _vcs);
	for (int o = 0; o < _outputs; ++o) {
		for (int v = 0; v < _vcs; ++v) {
			result[o * _vcs + v] = _next_buf[o]->LimitFor(v);
		}
	}
	return result;
}

void IQRouter::_UpdateNOQ(int input, int vc, Flit const * f) {
	assert(!_routing_delay);
	assert(f);
	assert(f->vc == vc);
	assert(f->head);
	set<OutputSet::sSetElement> sl = f->la_route_set.GetSet();
	assert(sl.size() == 1);
	int out_port = sl.begin()->output_port;
	const FlitChannel * channel = _output_channels[out_port];
	const Router * router = channel->GetSink();
	if (router) {
		int in_channel = channel->GetSinkPort();
		OutputSet nos;
		_rf(router, f, in_channel, &nos, false);
		sl = nos.GetSet();
		assert(sl.size() == 1);
		OutputSet::sSetElement const & se = *sl.begin();
		int next_output_port = se.output_port;
		assert(next_output_port >= 0);
		assert(_noq_next_output_port[input][vc] < 0);
		_noq_next_output_port[input][vc] = next_output_port;
		int next_vc_count = (se.vc_end - se.vc_start + 1) / router->NumOutputs();
		int next_vc_start = se.vc_start + next_output_port * next_vc_count;
		assert(next_vc_start >= 0 && next_vc_start < _vcs);
		assert(_noq_next_vc_start[input][vc] < 0);
		_noq_next_vc_start[input][vc] = next_vc_start;
		int next_vc_end = se.vc_start + (next_output_port + 1) * next_vc_count - 1;
		assert(next_vc_end >= 0 && next_vc_end < _vcs);
		assert(_noq_next_vc_end[input][vc] < 0);
		_noq_next_vc_end[input][vc] = next_vc_end;
		assert(next_vc_start <= next_vc_end);
		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Computing lookahead routing information for flit " << f->id
					<< " (NOQ)." << endl;
		}
	}
}

//------------------------------------------------------------------------------
// FLOV Facilities	// Jiayi
//------------------------------------------------------------------------------

void IQRouter::_FlovStep() {
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
		assert((input >= 0) && (input < _inputs - 1));

		Flit * const f = iter->second;
		assert(f);

		int const vc = f->vc;
		assert((vc >= 0) && (vc < _vcs));

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Bypass flit " << f->id << " to next router" << endl;
		}

		int output = input;
		if (output % 2)
			--output;
		else
			++output;
		assert((output >= 0) && (output < _outputs - 1));

		BufferState * const dest_buf = _next_buf[output];
		if (f->head)
			dest_buf->TakeBuffer(vc, _vcs * _inputs);	// indicate its taken by flov
		dest_buf->SendingFlit(f);

		if (f->watch) {
			*gWatchOut << GetSimTime() << " | " << FullName() << " | "
					<< "Buffering flit " << f->id << " at output " << output << "."
					<< endl;
		}
		_output_buffer[output].push(f);

    f->flov_hops++;

//#ifdef TRACK_FLOWS
//		++_stored_flits[f->cl][input];
//		if(f->head) ++_active_packets[f->cl][input];
//#endif
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

//#ifdef DEBUG_POWERGATE
//		if (GetSimTime() == 31 && _id == 37) {
//			cout << " Get the credit, before processing" << endl;
//			_next_buf[2]->Display(cout);
//		}
//#endif
		dest_buf->ProcessCredit(c);
//#ifdef DEBUG_POWERGATE
//		if (GetSimTime() == 31 && _id == 37) {
//			cout << " After processing" << endl;
//			_next_buf[2]->Display(cout);
//		}
//#endif
		// relay the credit to the upstream router
		// FLOV channel input output mapping
		// input --> output
		//	 0	 -->   1
		//	 1	 -->   0
		//	 2	 -->   3
		//	 3	 -->   2
		if (output != 4) {
			int input = output;
			if (output % 2)
				--input;
			else
				++input;
			assert((input >= 0) && (input < _inputs - 1));
			if ((output == 0 && _id % 8 == 0) || (output == 1 && _id % 8 == 7)
					|| (output == 2 && _id / 8 == 0) || (output == 3 && _id / 8 == 7))
				c->Free();
			else if (_power_state == wakeup && _downstream_state[input] == power_off)
				c->Free();
			else if (_out_queue_credits.count(input) == 0) {
				_out_queue_credits.insert(make_pair(input, c));
			}
		}

		if (output == 4)
			c->Free();
		_proc_credits.pop_front();
	}

	for (int in = 0; in < _inputs - 1; ++in) {
		int out = in;
		if (in % 2)
			--out;
		else
			++out;
		for (int vc = 0; vc < _vcs; ++vc) {
			if (_credit_counter[out][vc] > 0) {
				if (_out_queue_credits.count(in) == 0)
					_out_queue_credits.insert(make_pair(in, Credit::New()));
				if (_out_queue_credits.find(in)->second->vc.count(vc) == 0) {
					--_credit_counter[out][vc];
					_out_queue_credits.find(in)->second->vc.insert(vc);
				}
			}
		}
	}
}

void IQRouter::_HandShakeEvaluate() { // Should be evaluated before PowerStateEvaluate(), so put in ReadInputs()
	assert(_out_queue_handshakes.empty());

	vector<ePowerState> new_downstream_state = _downstream_state;

	while (!_proc_handshakes.empty()) {
		pair<int, Handshake *> const & item = _proc_handshakes.front();
		int const input = item.first;
		int output = input;
		assert((input >= 0) && (input < _inputs - 1));
		if (output % 2)
			--output;
		else
			++output;
		assert((output >= 0) && (output < _outputs - 1));

		Handshake * h = item.second;
		assert(h);
		int src_state = h->src_state;
		int new_state = h->new_state;

#ifdef DEBUG_POWERGATE
		if (_id == 31 && input == 3) {// && h->id == 55) {
			cout << GetSimTime() << " | router#" << _id << " receives handshake from router#" << h->id << " new state: "
					<< h->new_state << " src state: " << h->src_state << " drain tag: " << h->drain_done << endl;
      cout << "\tdrain done sent " << input << " is " << _drain_done_sent[input] << endl;
		}
		if (_id == 23 && input == 2) {// && h->id == 55) {
					cout << GetSimTime() << " | router#" << _id << " receives handshake from router#" << h->id << " new state: "
							<< h->new_state << " src state: " << h->src_state << " drain tag: " << h->drain_done << endl;
				}
//		if (_id == 55 && input == 1 && h->id == 53) {
//			cout << GetSimTime() << " | router#" << _id
//					<< " receives handshake from router#" << h->id << " new state: "
//					<< h->new_state << " src state: " << h->src_state << " drain tag: "
//					<< h->drain_done << endl;
//		}
//		if (_id == 54 && input == 1 && h->id == 53) {
//			cout << GetSimTime() << " | router#" << _id << " receives handshake from router#" << h->id << " new state: "
//					<< h->new_state << " src state: " << h->src_state << " drain tag: " << h->drain_done << endl;
//		}
#endif

		// power on
		if (_power_state == power_on) {
			if (src_state >= 0) {
				if (src_state == power_on) { // drain->on or wakeup->on
					assert(new_state == power_on);
					assert(
							_downstream_state[input] == draining
									|| _downstream_state[input] == wakeup);
					if (_downstream_state[input] == wakeup) {// wakeup->on
#ifdef DEBUG_POWERGATE
						if (!_drain_done_sent[input]) {
							cout << GetSimTime() << " | " << FullName()
									<< " | drain tag is not set, may be cleared by the relayed off.\n";
						}
#endif
						assert(_drain_done_sent[input]);
						_next_buf[input]->FullCredits();
						_drain_done_sent[input] = false;
					} else { // drain->on
						//_drain_done_sent[input] = false;
					}
				} else if (src_state == draining) { // on->drain
					assert(h->new_state == draining);
#ifdef DEBUG_POWERGATE
//					if (_downstream_state[input] != power_on) {
//						cout << GetSimTime() << " | " << FullName() << " | router#" << _id
//								<< " downstream router at output " << input << " is "
//								<< POWERSTATE[_downstream_state[input]] << endl;
//					}
#endif
					assert(_downstream_state[input] == power_on);
					_drain_done_sent[input] = false;
				} else if (src_state == wakeup) { // off->wakeup
					assert(_downstream_state[input] != wakeup);
					_drain_done_sent[input] = false;
				} else if (src_state == power_off) { // drain->off
					// it can be wakeup, since wakeup will relay power_off for flow control
					assert(_downstream_state[input] == draining || _downstream_state[input] == wakeup);
					// drain_tag must be sent for drain, but not received by wakeup, otherwise, it won't
					// off sice wakeup won't relay drain tag for draining. The sent tag will be cleared
					// by wakeup handshake
					if (_downstream_state[input] == draining) {
						assert(_drain_done_sent[input]);
						_drain_done_sent[input] = false;
					}
					_next_buf[input]->ClearCredits();
				}
			}
			if (h->drain_done)
				// I was draining previously
				// Or due to draining of my leftside and it back to on,
				// but I wakeup later, and use the first tag to on.
				//assert(0);
				; // do nothing
		}

		// draining
		else if (_power_state == draining) {
			if (src_state >= 0) {
				if (src_state == power_on) { // drain->on
					// if downstream routers are waking up, I cannot drain
					assert(_downstream_state[input] == draining);
					_drain_done_sent[input] = false;
				} else if (src_state == draining) {
					assert(_downstream_state[input] == power_on);
          _drain_done_sent[input] = false;
					// two routers goes to draining at similar time
					// do nothing, PowerStateEvaluate() will take care of this case,
					// only need to update the downstream state
				} else if (src_state == wakeup) { // off->wakeup
					assert(_downstream_state[input] != wakeup);
					_drain_done_sent[input] = false;
				} else if (src_state == power_off) {
					assert(0);	// should not happen, I cannot drain if downstream is draining
				}
			}
			if (h->drain_done) {
#ifdef DEBUG_POWERGATE
//        if (GetSimTime() == 20 && _id == 23) {
//          cout << "Wired! Anyway, got some clue." << endl;
//          cout << "Before, drain tags: " << _drain_tags[0] << " " << _drain_tags[1]
//              << " " << _drain_tags[2] << " " << _drain_tags[3] << endl;
//        }
#endif
				// if I'm draining, off router won't wakeup if they have my state,
				// if they wakeup at the similar time as I drain, no one will send tag to me
				// The one send the drain tag maybe is draining and back to on since I have
				// higher priority, and send the tag at the same time.
				assert(_downstream_state[input] == power_on || h->new_state == power_on);
				if (_drain_tags[input])
					cout << GetSimTime() << " | " << FullName() << " | router#" << _id
							<< " has received drain tag from input " << input << endl;
				assert(!_drain_tags[input]);
				_drain_tags[input] = true;
#ifdef DEBUG_POWERGATE
//        if (GetSimTime() == 20 && _id == 23) {
//          cout << "After, drain tags: " << _drain_tags[0] << " " << _drain_tags[1]
//              << " " << _drain_tags[2] << " " << _drain_tags[3] << endl;
//        }
#endif
			}
		}

		// power off
		else if (_power_state == power_off) {
			if (src_state >= 0) {
				if (src_state == power_on) {
					assert(
							_downstream_state[input] == draining
									|| _downstream_state[input] == wakeup);
					if (_downstream_state[input] == wakeup) { // wakeup->on
						for (int vc = 0; vc < _vcs; ++vc)
							_credit_counter[input][vc] = 0;
						_next_buf[input]->FullCredits();
					}
				} else if (src_state == draining) {
					assert(_downstream_state[input] == power_on);
				} else if (src_state == wakeup) {
#ifdef DEBUG_POWERGATE
//					if (_downstream_state[input] == wakeup) {
//						cout << GetSimTime() << " | router#" << _id << " recieves handshake from router#" << h->id
//								<< " at input " << input << ", new_state: " << h->new_state << ", src_state: " << h->src_state
//								<< ", drain tag: " << h->drain_done << endl;
//					}
#endif
					assert(_downstream_state[input] != wakeup || _downstream_state[input] != draining);
				} else if (src_state == power_off) {
					// if downstream is wakeup, it should send src state for flow control (clear credit counter)
					assert(_downstream_state[input] == draining || _downstream_state[input] == wakeup);
					for (int vc = 0; vc < _vcs; ++vc)
						_credit_counter[input][vc] = 0;
					_next_buf[input]->ClearCredits();
				}
			}
			if (h->drain_done) {
#ifdef DEBUG_POWERGATE
//				if (_downstream_state[output] == power_off) {
//					cout << GetSimTime() << " | router#" << _id
//							<< " receves handshake from router#" << h->id << " at input "
//							<< input << ", new_state: " << h->new_state << ", src_state: "
//							<< h->src_state << ", drain tag: " << h->drain_done << endl;
//				}
#endif
				// previous my right side are all off, so the left side routers can set tag directly,
				// later the draining is reflected to right side if one is wakeup, a tag is sent,
				// but the new state of left has been updated in my node but not in right side routers.
				//assert(_downstream_state[output] != power_off); // can from drain->on
				if (_downstream_state[output] != wakeup && _downstream_state[output] != draining)
					h->drain_done = false;
			}
		}

		// wake up
		else if (_power_state == wakeup) {
			if (src_state >= 0) {
				if (src_state == power_on) {
					assert(
							_downstream_state[input] == draining
									|| _downstream_state[input] == wakeup);
					if (_downstream_state[input] == wakeup) { // wakeup->on
						// wake up at similar time, drain tags are relayed to each other
						assert(_drain_tags[output]);
						for (int vc = 0; vc < _vcs; ++vc)
							_credit_counter[input][vc] = 0;
						_next_buf[input]->FullCredits();
//						if (!h->drain_done) {
//#ifdef DEBUG_POWERGATE
//							if (!_drain_tags[input]) {
//								cout << GetSimTime() << " | router#" << _id << " drain tag at input " << input
//										<< " is not set\n";
//							}
//#endif
//							// it could be, if the just power_on don't receive my wakeup state change
//							// before it sent the handshake, previous the line are all off
//							//assert(_drain_tags[input]);
//						}
					}
					//if (!h->drain_done)
					//	assert(_drain_tags[input]);
				} else if (src_state == draining) {
					assert(!h->drain_done);
					assert(_downstream_state[input] == power_on);
				} else if (src_state == wakeup) {
					assert(!h->drain_done);
					assert(_downstream_state[input] != wakeup);
					//if (_drain_tags[output]) {  // relay the drain tag
					if (_drain_tags[output] && _downstream_state[output] != power_off) {  // relay the drain tag
						if (_out_queue_handshakes.count(input) == 0) {
							_out_queue_handshakes.insert(make_pair(input, Handshake::New()));
							_out_queue_handshakes.find(input)->second->id = _id;
						}
						_out_queue_handshakes.find(input)->second->drain_done = true;
						_drain_done_sent[input] = true; // in case that when I wake up I send the tag again
					}
				} else if (src_state == power_off) {
					// if downstream is wakeup, it should send src state for flow control (clear credit counter)
					assert(_downstream_state[input] == draining || _downstream_state[input] == wakeup);
					for (int vc = 0; vc < _vcs; ++vc)
						_credit_counter[input][vc] = 0;
					_next_buf[input]->ClearCredits();
				}
			}
			if (h->drain_done) {
#ifdef DEBUG_POWERGATE
//				if (_drain_tags[input]) {
//					cout << GetSimTime() << " | " << FullName() << " | router#" << _id
//							<< "'s drain tag has been set but receive a new one at input "
//							<< input << " from router#" << h->id << endl;
//				}
#endif
				// FIXME: get two tags
				// could be, one tag is for previous draining of upstream router when I am off,
				// but later it back to on and I should wake up, then another tag may be sent
				// due to my state change (off->wakeup), but this tag is out dated.
				//assert(!_drain_tags[input]);
				_drain_tags[input] = true;
				assert(h->new_state == -1 || h->new_state == power_on);
				if (_downstream_state[output] == wakeup) {
					if (_out_queue_handshakes.count(output) == 0) {
						_out_queue_handshakes.insert(make_pair(output, h));
					}
					_drain_done_sent[output] = true;
				}
			}
		}

		// update downstream state
		if (h->new_state >= 0) {
#ifdef DEBUG_POWERGATE
//			if (_id == 0 && input == 2) {
//				cout << GetSimTime() << " | router#1 receive downstream router#"
//						<< h->id << " is " << POWERSTATE[h->new_state] << endl;
//			}
#endif
			new_downstream_state[input] = (ePowerState) h->new_state;
			//_downstream_state[input] = (ePowerState) h->new_state;
			if ((_id - h->id == -1) || (_id - h->id == 1) || (_id - h->id == -8)
					|| (_id - h->id == 8)) {
				assert(h->src_state >= 0);
				_neighbor_state[input] = (ePowerState) h->src_state;
			}
		}

		// free or relaying handshake
		if (_power_state == power_on || _power_state == draining)
			h->Free();
		else if (_power_state == wakeup) {
			if ((output == 0 && _id % 8 == 7) || (output == 1 && _id % 8 == 0)
					|| (output == 2 && _id / 8 == 7) || (output == 3 && _id / 8 == 0))
				h->Free();
			else if (_out_queue_handshakes.count(output) == 0) // don't need to relay drain tag
				if (h->src_state == power_off) { // need to relay power_off change for credit correctness
					h->new_state = -1;
					_out_queue_handshakes.insert(make_pair(output, h));
				} else
					h->Free();
			else if (_out_queue_handshakes.find(output)->second->id != h->id) {
				assert(_out_queue_handshakes.find(output)->second->id == _id);
				assert(_out_queue_handshakes.find(output)->second->drain_done);
				if (h->src_state == power_off)
					_out_queue_handshakes.find(output)->second->src_state = power_off;
				h->Free();
			} else {
				assert(h->drain_done);
				// should not relay the state transition, since mine has been sent
				_out_queue_handshakes.find(output)->second->new_state = -1; // don't relay downstream state
				if (h->src_state == power_off)
					_out_queue_handshakes.find(output)->second->src_state = power_off;
				else
					_out_queue_handshakes.find(output)->second->src_state = -1;
			}
		}	else {
			assert(_power_state == power_off);
			assert(_out_queue_handshakes.count(output) == 0);
			if ((output == 0 && _id % 8 == 7) || (output == 1 && _id % 8 == 0)
					|| (output == 2 && _id / 8 == 7) || (output == 3 && _id / 8 == 0))
				h->Free();
			else {
#ifdef DEBUG_POWERGATE
//				if (GetSimTime() == 2031 && _id == 54) {
//					cout << "router#" << _id << "'s downstream state#" << output << ": "
//							<< POWERSTATE[_downstream_state[output]] << ", handshake: new_state: "
//							<< h->new_state << ", src_state: " << h->src_state
//							<< ", drain done: " << h->drain_done << endl;
//				}
#endif
				// FIXME:
				if (!(_downstream_state[output] == draining || _downstream_state[output] == wakeup) && h->drain_done)
					h->drain_done = false;
#ifdef DEBUG_POWERGATE
//				if (GetSimTime() == 3777 && _id == 33) {
//					cout << "router#33's downstream state#" << output << ": "
//							<< POWERSTATE[_downstream_state[output]]
//							<< ", handshake: p_state: " << h->new_state << ", drain done: "
//							<< h->drain_done << endl;
//				}
#endif
				if (h->new_state != -1 || h->src_state != -1 || h->drain_done)
					_out_queue_handshakes.insert(make_pair(output, h));
				else
					h->Free();
			}
		}

		_proc_handshakes.pop_front();
	}

	_downstream_state = new_downstream_state;
}

void IQRouter::_HandShakeResponse() {
	assert(_power_state == power_on || _power_state == draining);

	for (int out_port = 0; out_port < _outputs - 1; ++out_port) {
		if (_downstream_state[out_port] == draining
				|| _downstream_state[out_port] == wakeup) {
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
				assert(
						_downstream_state[out_port] == draining
								|| _downstream_state[out_port] == wakeup);
				assert(!_drain_done_sent[out_port]);
#ifdef DEBUG_POWERGATE
			if (_id == 24 && out_port == 0) {
					cout << GetSimTime() << " | router#" << _id << " send drain done tag to downstream router at out port "
              << out_port << endl;
					cout << "Downstream state at out port " << out_port << " is " << POWERSTATE[_downstream_state[out_port]] << endl;
				}
#endif
				if (_out_queue_handshakes.count(out_port) == 0) {
					_out_queue_handshakes.insert(make_pair(out_port, Handshake::New()));
				} else {
						assert(_out_queue_handshakes.find(out_port)->second->new_state == _power_state);
				}
				_out_queue_handshakes.find(out_port)->second->drain_done = true;
				_out_queue_handshakes.find(out_port)->second->id = _id;
				_drain_done_sent[out_port] = true;
			}
		}
	}
}
