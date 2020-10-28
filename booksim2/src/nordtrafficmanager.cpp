/*
 * nordtrafficmanager.cpp
 * - A trafficmanager for NoRD
 *
 * Author: Jiayi Huang
 */

#include <sstream>
#include <cmath>
#include <fstream>
#include <limits>
#include <cstdlib>
#include <ctime>

#include "booksim.hpp"
#include "booksim_config.hpp"
#include "nordtrafficmanager.hpp"
#include "random_utils.hpp"
#include "vc.hpp"
#include "packet_reply_info.hpp"


NoRDTrafficManager::NoRDTrafficManager( const Configuration &config,
                                    const vector<Network *> & net )
    : TrafficManager(config, net)
{

    // ============ Traffic ============

    string packet_size_str = config.GetStr("packet_size");
    if(packet_size_str.empty()) {
        _packet_size.push_back(vector<int>(1, config.GetInt("packet_size")));
    } else {
        vector<string> packet_size_strings = tokenize_str(packet_size_str);
        for(size_t i = 0; i < packet_size_strings.size(); ++i) {
            _packet_size.push_back(tokenize_int(packet_size_strings[i]));
        }
    }
    _packet_size.resize(_classes, _packet_size.back());

    string packet_size_rate_str = config.GetStr("packet_size_rate");
    if(packet_size_rate_str.empty()) {
        int rate = config.GetInt("packet_size_rate");
        assert(rate >= 0);
        for(int c = 0; c < _classes; ++c) {
            int size = _packet_size[c].size();
            _packet_size_rate.push_back(vector<int>(size, rate));
            _packet_size_max_val.push_back(size * rate - 1);
        }
    } else {
        vector<string> packet_size_rate_strings = tokenize_str(packet_size_rate_str);
        packet_size_rate_strings.resize(_classes, packet_size_rate_strings.back());
        for(int c = 0; c < _classes; ++c) {
            vector<int> rates = tokenize_int(packet_size_rate_strings[c]);
            rates.resize(_packet_size[c].size(), rates.back());
            _packet_size_rate.push_back(rates);
            int size = rates.size();
            int max_val = -1;
            for(int i = 0; i < size; ++i) {
                int rate = rates[i];
                assert(rate >= 0);
                max_val += rate;
            }
            _packet_size_max_val.push_back(max_val);
        }
    }

    for(int c = 0; c < _classes; ++c) {
        if(_use_read_write[c]) {
            _packet_size[c] =
                vector<int>(1, (_read_request_size[c] + _read_reply_size[c] +
                                _write_request_size[c] + _write_reply_size[c]) / 2);
            _packet_size_rate[c] = vector<int>(1, 1);
            _packet_size_max_val[c] = 0;
        }
    }

    // ============ Statistics ============

    /* ==== Power Gate - Begin ==== */

    assert(config.GetInt("wait_for_tail_credit") > 0);

    vector<Router *> routers = _net[0]->GetRouters();
    for (int n = 0; n < _nodes; ++n) {
      if (n % gK == 0)
        cout << endl;
      cout << Router::POWERSTATE[routers[n]->GetPowerState()] << "\t";
    }
    cout << endl;

    // reset VC buffer depth for bypass latches
    vector<bool> const & router_states = _net[0]->GetRouterStates();
    for (int s = 0; s < _nodes; ++s) {
      // TODO: This will be set dynamically now, remove me should be fine
      //FlitChannel *ring_output_channel = routers[s]->GetRingOutputChannel();
      //int ring_next_router = ring_output_channel->GetSink()->GetID();
      //if (router_states[ring_next_router] == false) {
      //  routers[s]->SetRingOutputVCBufferSize(1);
      //}
      if (router_states[s] == false) {
        for (int subnet = 0; subnet < _subnets; ++subnet) {
          _buf_states[s][subnet]->SetVCBufferSize(1);
        }
      }
    }

    _buffers.resize(_nodes);
    for (int n = 0; n < _nodes; ++n) {
      _buffers[n].resize(_subnets);
      for (int subnet = 0; subnet < _subnets; ++subnet) {
        ostringstream tmp_name;
        tmp_name << "terminal_buf_" << n << "_" << subnet;
        _buffers[n][subnet] = new Buffer(config, 1, this, tmp_name.str());
      }
    }

    _during_bypassing.resize(_nodes);
    for (int s = 0; s < _nodes; ++s) {
      _during_bypassing[s].resize(_classes, false);
    }

    _routing_deadlock_timeout_threshold = config.GetInt("routing_deadlock_timeout_threshold");

    _performance_centric_wakeup_threshold = config.GetInt("nord_performance_centric_wakeup_threshold");
    _power_centric_wakeup_threshold = config.GetInt("nord_power_centric_wakeup_threshold");
    _wakeup_monitor_epoch = config.GetInt("nord_wakeup_monitor_epoch");
    _wakeup_monitor_vc_requests.resize(_nodes, 0);
    _performance_centric_routers.resize(_nodes, false);
    for (int n = 0; n < _nodes; ++n) {
      int row = n / gK;
      int col = n % gK;
      if ((row == 1) || (gK > 4 && row % 2 == 1 && row < gK - 1) ||
          (row == gK - 1 && col > 0 && col < gK - 1)) {
        _performance_centric_routers[n] = true;
      }
    }

    vector<int> watch_power_gating_routers = config.GetIntArray("watch_power_gating_routers");
    for (size_t i = 0; i < watch_power_gating_routers.size(); ++i) {
      _routers_to_watch_power_gating.insert(watch_power_gating_routers[i]);
    }
    /* ==== Power Gate - End ==== */
}

NoRDTrafficManager::~NoRDTrafficManager( )
{

}



void NoRDTrafficManager::_GeneratePacket( int source, int stype,
                                      int cl, int time )
{
    assert(stype!=0);

    /* ==== Power Gate - Begin ==== */
    vector<bool> & core_states = _net[0]->GetCoreStates();
    assert(core_states[source] == true);
    /* ==== Power Gate - End ==== */

    Flit::FlitType packet_type = Flit::ANY_TYPE;
    int size = _GetNextPacketSize(cl); //input size
    int pid = _cur_pid++;
    assert(_cur_pid);
    int packet_destination = _traffic_pattern[cl]->dest(source);
    /* ==== Power Gate - Begin ==== */
    if (_traffic[cl] == "tornado" && core_states[packet_destination] == false) {
        packet_destination = source;
    } else {
        while (core_states[packet_destination] != true)
            packet_destination = _traffic_pattern[cl]->dest(source);
    }
    assert(core_states[packet_destination] == true);
    /* ==== Power Gate - End ==== */
    bool record = false;
    bool watch = gWatchOut && (_packets_to_watch.count(pid) > 0);
    if(_use_read_write[cl]){
        if(stype > 0) {
            if (stype == 1) {
                packet_type = Flit::READ_REQUEST;
                size = _read_request_size[cl];
            } else if (stype == 2) {
                packet_type = Flit::WRITE_REQUEST;
                size = _write_request_size[cl];
            } else {
                ostringstream err;
                err << "Invalid packet type: " << packet_type;
                Error( err.str( ) );
            }
        } else {
            PacketReplyInfo* rinfo = _repliesPending[source].front();
            if (rinfo->type == Flit::READ_REQUEST) {//read reply
                size = _read_reply_size[cl];
                packet_type = Flit::READ_REPLY;
            } else if(rinfo->type == Flit::WRITE_REQUEST) {  //write reply
                size = _write_reply_size[cl];
                packet_type = Flit::WRITE_REPLY;
            } else {
                ostringstream err;
                err << "Invalid packet type: " << rinfo->type;
                Error( err.str( ) );
            }
            packet_destination = rinfo->source;
            time = rinfo->time;
            record = rinfo->record;
            _repliesPending[source].pop_front();
            rinfo->Free();
        }
    }

    if ((packet_destination <0) || (packet_destination >= _nodes)) {
        ostringstream err;
        err << "Incorrect packet destination " << packet_destination
            << " for stype " << packet_type;
        Error( err.str( ) );
    }

    if ( ( _sim_state == running ) ||
         ( ( _sim_state == draining ) && ( time < _drain_time ) ) ) {
        record = _measure_stats[cl];
    }

    int subnetwork = ((packet_type == Flit::ANY_TYPE) ?
                      RandomInt(_subnets-1) :
                      _subnet[packet_type]);

    if ( watch ) {
        *gWatchOut << GetSimTime() << " | "
                   << "node" << source << " | "
                   << "Enqueuing packet " << pid
                   << " at time " << time
                   << "." << endl;
    }

    for ( int i = 0; i < size; ++i ) {
        Flit * f  = Flit::New();
        f->id     = _cur_id++;
        assert(_cur_id);
        f->pid    = pid;
        f->watch  = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
        f->subnetwork = subnetwork;
        f->src    = source;
        f->ctime  = time;
        f->record = record;
        f->cl     = cl;

        _total_in_flight_flits[f->cl].insert(make_pair(f->id, f));
        if(record) {
            _measured_in_flight_flits[f->cl].insert(make_pair(f->id, f));
        }

        if(gTrace){
            cout<<"New Flit "<<f->src<<endl;
        }
        f->type = packet_type;

        if ( i == 0 ) { // Head flit
            f->head = true;
            //packets are only generated to nodes smaller or equal to limit
            f->dest = packet_destination;
            const vector<Router *> routers = _net[0]->GetRouters();
            f->ring_dest = routers[f->dest]->GetRingID();
        } else {
            f->head = false;
            //f->dest = -1; // XXX: for bypass
            f->dest = packet_destination;
        }
        switch( _pri_type ) {
        case class_based:
            f->pri = _class_priority[cl];
            assert(f->pri >= 0);
            break;
        case age_based:
            f->pri = numeric_limits<int>::max() - time;
            assert(f->pri >= 0);
            break;
        case sequence_based:
            f->pri = numeric_limits<int>::max() - _packet_seq_no[source];
            assert(f->pri >= 0);
            break;
        default:
            f->pri = 0;
        }
        if ( i == ( size - 1 ) ) { // Tail flit
            f->tail = true;
        } else {
            f->tail = false;
        }

        f->vc  = -1;

        if ( f->watch ) {
            *gWatchOut << GetSimTime() << " | "
                       << "node" << source << " | "
                       << "Enqueuing flit " << f->id
                       << " (packet " << f->pid
                       << ") at time " << time
                       << "." << endl;
        }

        _partial_packets[source][cl].push_back( f );
    }
}

void NoRDTrafficManager::_Inject()
{
    /* ==== Power Gate - Begin ==== */
    vector<bool> & core_states = _net[0]->GetCoreStates();
    /* ==== Power Gate - End ==== */

    for ( int input = 0; input < _nodes; ++input ) {
        /* ==== Power Gate - Begin ==== */
        if (core_states[input] == false)
            continue;
        /* ==== Power Gate - End ==== */
        for ( int c = 0; c < _classes; ++c ) {
            // Potentially generate packets for any (input,class)
            // that is currently empty
            if ( _partial_packets[input][c].empty() ) {
                bool generated = false;
                while( !generated && ( _qtime[input][c] <= _time ) ) {
                    int stype = _IssuePacket( input, c );

                    if ( stype != 0 ) { //generate a packet
                        _GeneratePacket( input, stype, c,
                                         _include_queuing==1 ?
                                         _qtime[input][c] : _time );
                        generated = true;
                    }
                    // only advance time if this is not a reply packet
                    if(!_use_read_write[c] || (stype >= 0)){
                        ++_qtime[input][c];
                    }
                }

                if ( ( _sim_state == draining ) &&
                     ( _qtime[input][c] > _drain_time ) ) {
                    _qdrained[input][c] = true;
                }
            }
        }
    }
}

void NoRDTrafficManager::_Step( )
{
  bool flits_in_flight = false;
  for(int c = 0; c < _classes; ++c) {
    flits_in_flight |= !_total_in_flight_flits[c].empty();
  }
  if(flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)){
    _deadlock_timer = 0;
    cout << GetSimTime() << " | ";
    cout << "WARNING: Possible network deadlock.\n";
    /* ==== Power Gate Debug - Begin ==== */
    const vector<Router *> routers = _net[0]->GetRouters();
    for (int n = 0; n < _nodes; ++n) {
      if (n % gK == 0)
        cout << endl;
      cout << Router::POWERSTATE[routers[n]->GetPowerState()] << "\t";
    }
    cout << endl;
    for (int n = 0; n < _nodes; ++n) {
      cout << "node " << n << " | "
        << routers[n]->FullName() << " | ["
        << Router::POWERSTATE[routers[n]->GetPowerState()] << "]";
      if (routers[n]->GetPowerState() == Router::power_off) {
        cout << " | Bypassing flit lists (if any):";
      }
      cout << endl;
      _buffers[n][0]->Display(cout);
      routers[n]->Display(cout);
      _buf_states[n][0]->Display(cout);
      cout << endl;
    }
    cout << endl;
    /* ==== Power Gate Debug - End ==== */
  }

  const vector<Router *> routers = _net[0]->GetRouters();
  const vector<bool> router_states = _net[0]->GetRouterStates();
  if (_performance_centric_wakeup_threshold > 0 &&
      _power_centric_wakeup_threshold > 0) {
    for (int n = 0; n < _nodes; ++n) {
      int wakeup_threshold = _performance_centric_routers[n] ?
        _performance_centric_wakeup_threshold :
        _power_centric_wakeup_threshold;
      if (_wakeup_monitor_vc_requests[n] > wakeup_threshold &&
          routers[n]->GetPowerState() == Router::power_on) {
        assert(router_states[n] == false);
        _wakeup_monitor_vc_requests[n] = 0;
        // TODO: no need to reset now, since off node won't inject flits, only bypass
        //for (int subnet = 0; subnet < _subnets; ++subnet) {
        //  _buf_states[n][subnet]->ResetVCBufferSize();
        //}
      }
    }
  }

  vector<map<int, Flit *> > flits(_subnets);

  for ( int subnet = 0; subnet < _subnets; ++subnet ) {
    for ( int n = 0; n < _nodes; ++n ) {
      Flit * const f = _net[subnet]->ReadFlit( n );
      if ( f ) {
        if(f->watch) {
          *gWatchOut << GetSimTime() << " | "
            << "node" << n << " | "
            << "Ejecting flit " << f->id
            << " (packet " << f->pid << ")"
            << " from VC " << f->vc
            << "." << endl;
        }
        flits[subnet].insert(make_pair(n, f));
        if((_sim_state == warming_up) || (_sim_state == running)) {
          if (f->dest == n) {
            ++_accepted_flits[f->cl][n];
            if(f->tail) {
              ++_accepted_packets[f->cl][n];
            }
          }
        }
      }

      Credit * const c = _net[subnet]->ReadCredit( n );
      if ( c ) {
#ifdef TRACK_FLOWS
        for(set<int>::const_iterator iter = c->vc.begin(); iter != c->vc.end(); ++iter) {
          int const vc = *iter;
          assert(!_outstanding_classes[n][subnet][vc].empty());
          int cl = _outstanding_classes[n][subnet][vc].front();
          _outstanding_classes[n][subnet][vc].pop();
          assert(_outstanding_credits[cl][subnet][n] > 0);
          --_outstanding_credits[cl][subnet][n];
        }
#endif
        if (_routers_to_watch_power_gating.count(n) > 0) {
          *gWatchOut << GetSimTime() << " | node " << n << " | "
            << "receives credit for bypass VCs";
          for (set<int>::iterator iter = c->vc.begin(); iter != c->vc.end(); ++iter) {
            *gWatchOut << " " << *iter;
          }
          *gWatchOut << endl;
        }
        _buf_states[n][subnet]->ProcessCredit(c);
        c->Free();
      }
    }
    _net[subnet]->ReadInputs( );
  }

  if ( !_empty_network ) {
    _Inject();
  }

  for(int subnet = 0; subnet < _subnets; ++subnet) {

    for(int n = 0; n < _nodes; ++n) {

      Flit * f = nullptr;

      BufferState * const dest_buf = _buf_states[n][subnet];

      int const last_class = _last_class[n][subnet];

      int class_limit = _classes;

      /* ==== Power Gate - Begin ==== */
      int const last_vc = _last_vc[n][subnet][last_class];
      int vc_limit = _vcs;
      int bypass_vc = -1;
      /* ==== Power Gate - End ==== */

      if (!_during_bypassing[n][last_class]) {
        if(_hold_switch_for_packet) {
          list<Flit *> const & pp = _partial_packets[n][last_class];
          if(!pp.empty() && !pp.front()->head &&
              !dest_buf->IsFullFor(pp.front()->vc)) {
            f = pp.front();
            assert(f->vc == _last_vc[n][subnet][last_class]);

            // if we're holding the connection, we don't need to check that class
            // again in the for loop
            --class_limit;
          }
        }

        for(int i = 1; i <= class_limit; ++i) {

          int const c = (last_class + i) % _classes;

          list<Flit *> const & pp = _partial_packets[n][c];

          if(pp.empty()) {
            continue;
          }

          Flit * const cf = pp.front();
          assert(cf);
          assert(cf->cl == c);

          if(cf->subnetwork != subnet) {
            continue;
          }

          if(f && (f->pri >= cf->pri)) {
            continue;
          }

          if(cf->head && cf->vc == -1) { // Find first available VC

            OutputSet route_set;
            _rf(nullptr, cf, -1, &route_set, true);
            set<OutputSet::sSetElement> const & os = route_set.GetSet();
            assert(os.size() == 1);
            OutputSet::sSetElement const & se = *os.begin();
            assert(se.output_port == -1);
            int vc_start = se.vc_start;
            int vc_end = se.vc_end;
            int vc_count = vc_end - vc_start + 1;
            assert(!_noq);
            if(cf->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "Finding output VC for flit " << cf->id
                << ":" << endl;
            }
            for(int i = 1; i <= vc_count; ++i) {
              int const lvc = _last_vc[n][subnet][c];
              int const vc =
                (lvc < vc_start || lvc > vc_end) ?
                vc_start :
                (vc_start + (lvc - vc_start + i) % vc_count);
              assert((vc >= vc_start) && (vc <= vc_end));
              if(!dest_buf->IsAvailableFor(vc)) {
                if(cf->watch) {
                  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                    << "  Output VC " << vc << " is busy." << endl;
                }
              } else {
                if(dest_buf->IsFullFor(vc)) {
                  if(cf->watch) {
                    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                      << "  Output VC " << vc << " is full." << endl;
                  }
                } else {
                  if(cf->watch) {
                    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                      << "  Selected output VC " << vc << "." << endl;
                  }
                  cf->vc = vc;
                  break;
                }
              }
            }
          }

          if(cf->vc == -1) {
            if(cf->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "No output VC found for flit " << cf->id
                << "." << endl;
            }
          } else {
            if(dest_buf->IsFullFor(cf->vc)) {
              if(cf->watch) {
                *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                  << "Selected output VC " << cf->vc
                  << " is full for flit " << cf->id
                  << "." << endl;
              }
            } else {
              f = cf;
            }
          }
        }

      } else {
        /* ==== Power Gate - Begin ==== */
        if(_hold_switch_for_packet) {
          for (int vc = 0; vc < _vcs; vc++) {
            if (_buffers[n][subnet]->GetState(vc) == VC::active &&
                vc == last_vc && !dest_buf->IsFullFor(vc)) {
              int out_vc = _buffers[n][subnet]->GetOutputVC(vc);
              assert(out_vc >= 0 && out_vc < _vcs);
              if (!dest_buf->IsFullFor(out_vc)) {
                f = _buffers[n][subnet]->FrontFlit(vc);
                if (f && f->cl == last_class) {
                  if (f->watch) {
                    *gWatchOut << GetSimTime() << " | node " << n
                      << " | flit " << f->id << " is selected for hold switch bypass"
                      << endl;
                  }
                  f->vc = _buffers[n][subnet]->GetOutputVC(vc);
                  bypass_vc = vc;
                  vc_limit--;
                  break;
                } else {
                  f = nullptr;
                }
              }
            }
          }
        }

        for(int i = 1; i <= vc_limit; ++i) {

          int const vc = (last_vc + i) % _vcs;

          if (_buffers[n][subnet]->GetState(vc) == VC::idle) {
            continue;
          }

          Flit * const cf = _buffers[n][subnet]->FrontFlit(vc);
          if (cf == nullptr) {
            continue;
          }

          if(f && (f->pri >= cf->pri)) {
            assert(!f->head);
            assert(f->vc != -1);
            assert(_buffers[n][subnet]->GetState(bypass_vc) == VC::active);
            continue;
          }

          if (_buffers[n][subnet]->GetState(vc) == VC::vc_alloc) { // Find first available VC

            assert(cf->head);

            const vector<Router *> routers = _net[0]->GetRouters();

            if (_performance_centric_wakeup_threshold > 0 &&
                _power_centric_wakeup_threshold > 0) {
              int wakeup_threshold = _performance_centric_routers[n] ?
                _performance_centric_wakeup_threshold :
                _power_centric_wakeup_threshold;
              ++_wakeup_monitor_vc_requests[n];
              if (_time % _wakeup_monitor_epoch == 0) {
                if (_wakeup_monitor_vc_requests[n] >= wakeup_threshold) {
                  routers[n]->WakeUp();
                }
              }
            }

            OutputSet route_set;
            _rf(routers[n], cf, -1, &route_set, true);
            set<OutputSet::sSetElement> const & os = route_set.GetSet();
            for (set<OutputSet::sSetElement>::const_iterator iset = os.begin();
                iset != os.end(); ++iset)
            {
              OutputSet::sSetElement const & se = *(iset);
              assert(se.output_port == -1);
              bool found;
              int vc_start = se.vc_start;
              int vc_end = se.vc_end;
              int vc_count = vc_end - vc_start + 1;
              assert(!_noq);
              if(cf->watch) {
                *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                  << "Finding output VC for flit " << cf->id
                  << ":" << endl;
              }
              for(int i = 1; i <= vc_count; ++i) {
                int const lvc = last_vc;
                int const out_vc =
                  (lvc < vc_start || lvc > vc_end) ?
                  vc_start :
                  (vc_start + (lvc - vc_start + i) % vc_count);
                assert((out_vc >= vc_start) && (out_vc <= vc_end));
                if (out_vc < 0 || out_vc >= _vcs) {
                  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                    << "Finding output VC for flit " << cf->id
                    << ":" << endl;
                }
                if(!dest_buf->IsAvailableFor(out_vc)) {
                  if(cf->watch) {
                    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                      << "  Output VC " << out_vc << " is busy." << endl;
                  }
                } else {
                  if(dest_buf->IsFullFor(out_vc)) {
                    if(cf->watch) {
                      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                        << "  Output VC " << out_vc << " is full." << endl;
                    }
                  } else {
                    if(cf->watch) {
                      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                        << "  Selected output VC " << out_vc << "." << endl;
                    }
                    _buffers[n][subnet]->SetOutput(vc, -1, out_vc);
                    _buffers[n][subnet]->SetState(vc, VC::active);
                    cf->vc = out_vc;
                    found = true;
                    break;
                  }
                }
              }
              if (found) break;
            }
          } else if (!cf->head && cf->vc == -1) {
            assert(cf->src != n);
            assert(_buffers[n][subnet]->GetState(vc) == VC::active);
            cf->vc = _buffers[n][subnet]->GetOutputVC(vc);
          }

          if(cf->vc == -1) {
            if(cf->watch) {
              *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << "No output VC found for flit " << cf->id
                << "." << endl;
            }
            if (cf->head && _time - cf->rtime > 300) {
              _buffers[n][subnet]->SetState(vc, VC::vc_alloc);
            }
          } else {
            assert(_buffers[n][subnet]->GetState(vc) == VC::active);
            if(dest_buf->IsFullFor(cf->vc)) {
              if(cf->watch) {
                *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                  << "Selected output VC " << cf->vc
                  << " is full for flit " << cf->id
                  << "." << endl;
              }
              if (cf->head && _time - cf->rtime > 300) {
                _buffers[n][subnet]->SetState(vc, VC::vc_alloc);
              }
            } else {
              f = cf;
              bypass_vc = vc;
              if (f->head) {
                assert(f->src != n);
              }
              break;
            }
          }
        }
      }

      if(f) {

        assert(f->subnetwork == subnet);

        int const c = f->cl;

        if(f->head) {

          if (_lookahead_routing) {
            assert(!_noq);
            const FlitChannel * inject = _net[subnet]->GetInject(n);
            const Router * router = inject->GetSink();
            assert(router);
            int in_channel = inject->GetSinkPort();
            _rf(router, f, in_channel, &f->la_route_set, false);
            if (f->watch) {
                *gWatchOut << GetSimTime() << " | "
                    << "node" << n << " | "
                    << "Generating lookahead routing info for flit " << f->id
                    << "." << endl;
            }
          } else {
            f->la_route_set.Clear();
          }

          dest_buf->TakeBuffer(f->vc);
          _last_vc[n][subnet][c] = f->vc;
        }

        _last_class[n][subnet] = c;

        list<Flit *> & pp = _partial_packets[n][c];
        /* ==== Power Gate - Begin ==== */
        if (_during_bypassing[n][c]) {
          assert(_partial_packets[n][c].empty() ||
              _partial_packets[n][c].front()->head);
          _buffers[n][subnet]->RemoveFlit(bypass_vc);
        } else {
          pp.pop_front();
        }
        /* ==== Power Gate - End ==== */

#ifdef TRACK_FLOWS
        ++_outstanding_credits[c][subnet][n];
        _outstanding_classes[n][subnet][f->vc].push(c);
#endif

        dest_buf->SendingFlit(f);

        if(_pri_type == network_age_based) {
          f->pri = numeric_limits<int>::max() - _time;
          assert(f->pri >= 0);
        }

        if(f->watch) {
          if (f->src == n)
            *gWatchOut << GetSimTime() << " | "
              << "node" << n << " | "
              << "Injecting flit " << f->id
              << " into subnet " << subnet
              << " at time " << _time
              << " with priority " << f->pri
              << "." << endl;
          else
            /* ==== Power Gate - Begin ==== */
            *gWatchOut << GetSimTime() << " | "
              << "node" << n << " | "
              << "Bypassing flit " << f->id
              << " into subnet " << subnet
              << " to VC " << f->vc
              << " at time " << _time
              << " with priority " << f->pri
              << "." << endl;
            /* ==== Power Gate - End ==== */
        }

        if (f->src == n) {
          f->itime = _time;

          // Pass VC "back"
          if(!pp.empty() && !f->tail) {
            Flit * const nf = pp.front();
            assert(nf->pid == f->pid);
            nf->vc = f->vc;
          }
        } else {
          /* ==== Power Gate - Begin ==== */
          // send credit for bypass latch
          Credit * const c = Credit::New();
          c->vc.insert(f->bypass_vc);
          if(f->watch) {
            *gWatchOut << GetSimTime() << " | "
              << "node" << n << " | "
              << "Injecting credit for (bypass) VC " << f->bypass_vc
              << " into subnet " << subnet
              << "." << endl;
          }
          f->bypass_vc = -1;
          _net[subnet]->WriteCredit(c, n);
          /* ==== Power Gate - End ==== */
        }

        /* ==== Power Gate - Begin ==== */
        if (f->tail) {
          if (_during_bypassing[n][c]) {
            _buffers[n][subnet]->SetState(bypass_vc, VC::idle);
          } else {
            assert(f->src == n);
          }
          for (int vc = 0; vc < _vcs; ++vc) {
            if (_buffers[n][subnet]->GetState(vc) != VC::idle) {
              _during_bypassing[n][c] = true;
              break;
            }
          }
          // TODO: handle starvation of normal injection
        }
        /* ==== Power Gate - End ==== */

        if((_sim_state == warming_up) || (_sim_state == running)) {
          /* ==== Power Gate - Begin ==== */
          if (f->src == n) {
            ++_sent_flits[c][n];
            if(f->head) {
              ++_sent_packets[c][n];
            }
          }
          /* ==== Power Gate - End ==== */
        }

#ifdef TRACK_FLOWS
        /* ==== Power Gate - Begin ==== */
        if (f->src == n)
          ++_injected_flits[c][n];
        /* ==== Power Gate - End ==== */
#endif

        _net[subnet]->WriteFlit(f, n);

      }
    }
  }

  for(int subnet = 0; subnet < _subnets; ++subnet) {
    for(int n = 0; n < _nodes; ++n) {
      map<int, Flit *>::const_iterator iter = flits[subnet].find(n);
      if(iter != flits[subnet].end()) {
        Flit * const f = iter->second;

        if (f->dest == n) {

          f->atime = _time;
          if(f->watch) {
            *gWatchOut << GetSimTime() << " | "
              << "node" << n << " | "
              << "Injecting credit for VC " << f->vc
              << " into subnet " << subnet
              << "." << endl;
          }
          Credit * const c = Credit::New();
          c->vc.insert(f->vc);
          _net[subnet]->WriteCredit(c, n);

#ifdef TRACK_FLOWS
          ++_ejected_flits[f->cl][n];
#endif

          _RetireFlit(f, n);

        } else {

          /* ==== Power Gate - Begin ==== */
          f->rtime = _time;
          if (f->watch) {
            *gWatchOut << GetSimTime() << " | "
              << "node" << n << " | "
              << "receives bypass flit " << f->id
              << " (pid: " << f->pid
              << ") and buffer at VC " << f->vc
              << "." << endl;
          }

          int vc = f->vc;
          f->bypass_vc = f->vc;
          f->vc = -1;
          assert(_buffers[n][subnet]->Empty(vc));
          _buffers[n][subnet]->AddFlit(vc, f);
          if (f->head) {
            // XXX: a packet can be reroute back to this node
            assert(_buffers[n][subnet]->GetState(vc) == VC::idle);
            _buffers[n][subnet]->SetState(vc, VC::vc_alloc);
          }
          if (_partial_packets[n][f->cl].empty() ||
              (_partial_packets[n][f->cl].front()->head &&
               _partial_packets[n][f->cl].front()->vc == -1)) {
            _during_bypassing[n][f->cl] = true;
          }
          /* ==== Power Gate - End ==== */
        }
      }
    }
    flits[subnet].clear();
    /* ==== Power Gate - Begin ==== */
    _net[subnet]->PowerStateEvaluate();
    /* ==== Power Gate - End ==== */
    _net[subnet]->Evaluate( );
    _net[subnet]->WriteOutputs( );
  }

  ++_time;
  assert(_time);
  if(gTrace){
    cout<<"TIME "<<_time<<endl;
  }

}

