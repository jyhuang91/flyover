/*
 * flovtrafficmanager.cpp
 * - A traffic manager for FlyOver
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
#include "flovtrafficmanager.hpp"
#include "random_utils.hpp"
#include "vc.hpp"
#include "packet_reply_info.hpp"

FLOVTrafficManager::FLOVTrafficManager( const Configuration &config,
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
    _flov_hop_stats.resize(_classes);
    _overall_flov_hop_stats.resize(_classes, 0.0);

    for (int c = 0; c < _classes; ++c) {
        ostringstream tmp_name;

        tmp_name << "flov_hop_stat_" << c;
        _flov_hop_stats[c] = new Stats(this, tmp_name.str(), 1.0, 20);
        _stats[tmp_name.str()] = _hop_stats[c];
        tmp_name.str("");
    }

    _router_idle_periods.resize(_nodes, 0);
    _idle_cycles.resize(_nodes);
    _overall_idle_cycles.resize(_nodes);
    for (int n = 0; n < _nodes; ++n) {
      _idle_cycles[n].resize(320000, 0);
      _overall_idle_cycles[n].resize(32000, 0);
    }
    _wakeup_handshake_latency.resize(_nodes, false);

    _monitor_counter = 0;
    _monitor_epoch = config.GetInt("flov_monitor_epoch");
    double high_watermark = config.GetFloat("high_watermark");
    double low_watermark = config.GetFloat("low_watermark");
    double zeroload_latency = config.GetFloat("zeroload_latency");
    _plat_high_watermark = zeroload_latency * high_watermark;
    _plat_low_watermark = zeroload_latency * low_watermark;

    _powergate_type = config.GetStr("powergate_type");
    _power_state_votes.resize(_nodes, 0);
    _per_node_plat.resize(_nodes);
    for (int n = 0; n < _nodes; ++n) {
      ostringstream tmp_name;

      tmp_name << "per_node_plat_stat_" << n;
      _per_node_plat[n] = new Stats(this, tmp_name.str(), 1.0, 1000);
      tmp_name.str("");
    }
    /* ==== Power Gate - End ==== */
}

FLOVTrafficManager::~FLOVTrafficManager( )
{

    /* ==== Power Gate - Begin ==== */
    for ( int c = 0; c < _classes; ++c ) {
        delete _flov_hop_stats[c];
    }
    /* ==== Power Gate - End ==== */
}

void FLOVTrafficManager::_RetireFlit( Flit *f, int dest )
{
    _deadlock_timer = 0;

    assert(_total_in_flight_flits[f->cl].count(f->id) > 0);
    _total_in_flight_flits[f->cl].erase(f->id);

    if(f->record) {
        assert(_measured_in_flight_flits[f->cl].count(f->id) > 0);
        _measured_in_flight_flits[f->cl].erase(f->id);
    }

    if ( f->watch ) {
        *gWatchOut << GetSimTime() << " | "
                   << "node" << dest << " | "
                   << "Retiring flit " << f->id
                   << " (packet " << f->pid
                   << ", src = " << f->src
                   << ", dest = " << f->dest
                   << ", hops = " << f->hops
                   /* ==== Power Gate - Begin ==== */
                   << ", flov hops = " << f->flov_hops
                   /* ==== Power Gate - End ==== */
                   << ", flat = " << f->atime - f->itime
                   << ")." << endl;
    }

    if ( f->head && ( f->dest != dest ) ) {
        ostringstream err;
        err << "Flit " << f->id << " arrived at incorrect output " << dest;
        Error( err.str( ) );
    }

    if((_slowest_flit[f->cl] < 0) ||
       (_flat_stats[f->cl]->Max() < (f->atime - f->itime)))
        _slowest_flit[f->cl] = f->id;
    _flat_stats[f->cl]->AddSample( f->atime - f->itime);
    if(_pair_stats){
        _pair_flat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->itime );
    }

    if ( f->tail ) {
        Flit * head;
        if(f->head) {
            head = f;
        } else {
            map<int, Flit *>::iterator iter = _retired_packets[f->cl].find(f->pid);
            assert(iter != _retired_packets[f->cl].end());
            head = iter->second;
            _retired_packets[f->cl].erase(iter);
            assert(head->head);
            assert(f->pid == head->pid);
        }
        if ( f->watch ) {
            *gWatchOut << GetSimTime() << " | "
                       << "node" << dest << " | "
                       << "Retiring packet " << f->pid
                       << " (plat = " << f->atime - head->ctime
                       << ", nlat = " << f->atime - head->itime
                       << ", frag = " << (f->atime - head->atime) - (f->id - head->id) // NB: In the spirit of solving problems using ugly hacks, we compute the packet length by taking advantage of the fact that the IDs of flits within a packet are contiguous.
                       << ", src = " << head->src
                       << ", dest = " << head->dest
                       << ")." << endl;
        }

        //code the source of request, look carefully, its tricky ;)
        if (f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST) {
            PacketReplyInfo* rinfo = PacketReplyInfo::New();
            rinfo->source = f->src;
            rinfo->time = f->atime;
            rinfo->record = f->record;
            rinfo->type = f->type;
            _repliesPending[dest].push_back(rinfo);
        } else {
            if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
                _requestsOutstanding[dest]--;
            } else if(f->type == Flit::ANY_TYPE) {
                _requestsOutstanding[f->src]--;
            }

        }

        // Only record statistics once per packet (at tail)
        // and based on the simulation state
        if ( ( _sim_state == warming_up ) || f->record ) {

            _hop_stats[f->cl]->AddSample( f->hops );
            /* ==== Power Gate - Begin ==== */
            _flov_hop_stats[f->cl]->AddSample(f->flov_hops);
            _per_node_plat[dest]->AddSample(f->atime - head->ctime);
            /* ==== Power Gate - End ==== */

            if((_slowest_packet[f->cl] < 0) ||
               (_plat_stats[f->cl]->Max() < (f->atime - head->itime)))
                _slowest_packet[f->cl] = f->pid;
            _plat_stats[f->cl]->AddSample( f->atime - head->ctime);
            _nlat_stats[f->cl]->AddSample( f->atime - head->itime);
            _frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );

            if(_pair_stats){
                _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - head->ctime );
                _pair_nlat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - head->itime );
            }
        }

        if(f != head) {
            head->Free();
        }

    }

    if(f->head && !f->tail) {
        _retired_packets[f->cl].insert(make_pair(f->pid, f));
    } else {
        f->Free();
    }
}


void FLOVTrafficManager::_GeneratePacket( int source, int stype,
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
    watch |= _watch_all_packets;
    if (_use_read_write[cl]){
        if (stype > 0) {
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
            } else if (rinfo->type == Flit::WRITE_REQUEST) {  //write reply
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

    if ((packet_destination < 0) || (packet_destination >= _nodes)) {
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
        } else {
            f->head = false;
            f->dest = -1;
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

void FLOVTrafficManager::_Inject()
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

void FLOVTrafficManager::_Step( )
{
    bool flits_in_flight = false;
    for(int c = 0; c < _classes; ++c) {
        flits_in_flight |= !_total_in_flight_flits[c].empty();
    }
    if(flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)){
        _deadlock_timer = 0;
        cout << "WARNING: Possible network deadlock.\n";
        /* ==== Power Gate Debug - Begin ==== */
        cout << GetSimTime() << endl;
        const vector<Router *> routers = _net[0]->GetRouters();
        for (int n = 0; n < _nodes; ++n) {
            if (n % gK == 0)
                cout << endl;
            cout << Router::POWERSTATE[routers[n]->GetPowerState()] << "\t";
        }
        cout << endl;
        for (int n = 0; n < _nodes; ++n)
            routers[n]->Display(cout);
        cout << endl << endl;
        /* ==== Power Gate Debug - End ==== */
    }

    /* ==== Power Gate - Begin ==== */
    // adaptive power-gating
    if (_powergate_type == "flov" && _monitor_counter / _monitor_epoch > 0) {
      int turn = _monitor_counter % _monitor_epoch;
      for (int row = 0; row < gK; ++row) {
        for (int col = 0; col < gK; ++col) {
          if (row == turn || col == turn) {
            int n = row * gK + col;
            if (_per_node_plat[n]->NumSamples() == 0)
              continue;

            int vote = 0;
            double avg_plat = _per_node_plat[n]->Average();
            if (avg_plat < _plat_low_watermark) {
              vote = 1;
            } else if (avg_plat > _plat_high_watermark) {
              vote = -1;
            }

            if (row == turn) {
              for (int c = 0; c < gK; ++c) {
                if (c == col)
                  continue;

                int node = row * gK + c;
                _power_state_votes[node] += vote;
              }
            }
            if (col == turn) {
              for (int r = 0; r < gK; ++r) {
                if (r == row)
                  continue;

                int node = r * gK + col;
                _power_state_votes[node] += vote;
              }
            }
            _power_state_votes[n] += vote;

            _per_node_plat[n]->Clear();
          }
        }
      }

      if (turn == gK) {
        vector<Router *> routers = _net[0]->GetRouters();
        for (int n = 0; n < _nodes; ++n) {
          if (n >= _nodes - gK) {
            _power_state_votes[n] = 0;
            continue;
          }

          if (_power_state_votes[n] > 0) {
            routers[n]->AggressPowerGatingPolicy();
            //cout << GetSimTime() << " | node " << n
            //  << " | (vote " << _power_state_votes[n]
            //  << ") can switch off to save more power" << endl;
          } else if (_power_state_votes[n] < 0) {
            routers[n]->RegressPowerGatingPolicy();
            //cout << GetSimTime() << " | node " << n
            //  << " | (vote " << _power_state_votes[n]
            //  << ") should switch on for performance" << endl;
          }
          _power_state_votes[n] = 0;
        }
        _monitor_counter = 0;
      }
    }

    for (int n = 0; n < _nodes; ++n) {
        bool is_idle = true;
        bool is_inj_busy = false;
        for (int c = 0; c < _classes; ++c) { // injection
            list<Flit *> const & flist = _partial_packets[n][c];
            if (!flist.empty()) {
                is_inj_busy = true;
                break;
            }
        }
        for (int subnet = 0; subnet < _subnets; ++subnet) {
            Flit * const f = _net[subnet]->ReadFlit(n); // ejection
            if (is_inj_busy || f) {
                is_idle = false;
                break;
            }
        }
        if (is_idle == true) {  // found an idle cycle
            for (int subnet = 0; subnet < _subnets; ++subnet) {
                vector<Router *> routers = _net[subnet]->GetRouters();
                routers[n]->IdleDetected();
            }
            ++_router_idle_periods[n];
        } else {    // busy for any subnetwork with the node
            int cur_idle_cycles = _router_idle_periods[n];
            if (cur_idle_cycles >= _overall_idle_cycles[n].size()) {
                for (int i = 0; i < _nodes; ++i) {
                    _idle_cycles[i].resize(cur_idle_cycles + 10, 0);
                    _overall_idle_cycles[i].resize(cur_idle_cycles + 10, 0);
                }
            }
            if (cur_idle_cycles != 0) { // for the busy cycle
                ++_idle_cycles[n][0];
                ++_overall_idle_cycles[n][0];
            }
            ++_idle_cycles[n][cur_idle_cycles];
            ++_overall_idle_cycles[n][cur_idle_cycles];
            _router_idle_periods[n] = 0;
            // Power on Router
            for (int subnet = 0; subnet < _subnets; ++subnet) {
                vector<Router *> routers = _net[subnet]->GetRouters();
                routers[n]->WakeUp();
            }
        }
    }
    /* ==== Power Gate - End ==== */

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
                    ++_accepted_flits[f->cl][n];
                    if(f->tail) {
                        ++_accepted_packets[f->cl][n];
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

            Flit * f = NULL;

            BufferState * const dest_buf = _buf_states[n][subnet];

            int const last_class = _last_class[n][subnet];

            int class_limit = _classes;

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

                    /* ==== Power Gate - Begin ==== */
                    // should not send if router is sleeping, wake it up
                    const FlitChannel * inject = _net[subnet]->GetInject(n);
                    Router * router = inject->GetSink();
                    assert(router);
                    if (router->GetPowerState() != Router::power_on) {
                        if (_wakeup_handshake_latency[n]) {
                            router->WakeUp();
                            _wakeup_handshake_latency[n] = false;
                        } else
                            _wakeup_handshake_latency[n] = true;
                        if (cf->watch) {
                            *gWatchOut << GetSimTime() << " | "
                                << "node_" << n << " | "
                                << "Attached router is not power on, wake it up for flit "
                                << cf->id << "." << endl;
                        }
                        break;
                    } else {
                        _wakeup_handshake_latency[n] = false;
                    }
                    /* ==== Power Gate - End ==== */

                    OutputSet route_set;
                    _rf(NULL, cf, -1, &route_set, true);
                    set<OutputSet::sSetElement> const & os = route_set.GetSet();
                    assert(os.size() == 1);
                    OutputSet::sSetElement const & se = *os.begin();
                    assert(se.output_port == -1);
                    int vc_start = se.vc_start;
                    int vc_end = se.vc_end;
                    int vc_count = vc_end - vc_start + 1;
                    if(_noq) {
                        assert(_lookahead_routing);
                        const FlitChannel * inject = _net[subnet]->GetInject(n);
                        const Router * router = inject->GetSink();
                        assert(router);
                        int in_channel = inject->GetSinkPort();

                        // NOTE: Because the lookahead is not for injection, but for the
                        // first hop, we have to temporarily set cf's VC to be non-negative
                        // in order to avoid seting of an assertion in the routing function.
                        cf->vc = vc_start;
                        _rf(router, cf, in_channel, &cf->la_route_set, false);
                        cf->vc = -1;

                        if(cf->watch) {
                            *gWatchOut << GetSimTime() << " | "
                                       << "node" << n << " | "
                                       << "Generating lookahead routing info for flit " << cf->id
                                       << " (NOQ)." << endl;
                        }
                        set<OutputSet::sSetElement> const sl = cf->la_route_set.GetSet();
                        assert(sl.size() == 1);
                        int next_output = sl.begin()->output_port;
                        vc_count /= router->NumOutputs();
                        vc_start += next_output * vc_count;
                        vc_end = vc_start + vc_count - 1;
                        assert(vc_start >= se.vc_start && vc_start <= se.vc_end);
                        assert(vc_end >= se.vc_start && vc_end <= se.vc_end);
                        assert(vc_start <= vc_end);
                    }
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

            if(f) {

                assert(f->subnetwork == subnet);

                int const c = f->cl;

                if(f->head) {

                    /* ==== Power Gate - Begin ==== */
                    // should not send if router is sleeping, wake it up
                    const FlitChannel * inject = _net[subnet]->GetInject(n);
                    Router * router = inject->GetSink();
                    assert(router);
                    if (router->GetPowerState() != Router::power_on) {
                        if (_wakeup_handshake_latency[n]) {
                            router->WakeUp();
                            _wakeup_handshake_latency[n] = false;
                        } else
                            _wakeup_handshake_latency[n] = true;
                        if (f->watch) {
                            *gWatchOut << GetSimTime() << " | "
                                << "node_" << n << " | "
                                << "Attached router is not power on, wake it up for flit "
                                << f->id << "." << endl;
                        }
                        break;
                    } else {
                        _wakeup_handshake_latency[n] = false;
                    }
                    /* ==== Power Gate - End ==== */

                    if (_lookahead_routing) {
                        if(!_noq) {
                            const FlitChannel * inject = _net[subnet]->GetInject(n);
                            const Router * router = inject->GetSink();
                            assert(router);
                            int in_channel = inject->GetSinkPort();
                            _rf(router, f, in_channel, &f->la_route_set, false);
                            if(f->watch) {
                                *gWatchOut << GetSimTime() << " | "
                                           << "node" << n << " | "
                                           << "Generating lookahead routing info for flit " << f->id
                                           << "." << endl;
                            }
                        } else if(f->watch) {
                            *gWatchOut << GetSimTime() << " | "
                                       << "node" << n << " | "
                                       << "Already generated lookahead routing info for flit " << f->id
                                       << " (NOQ)." << endl;
                        }
                    } else {
                        f->la_route_set.Clear();
                    }

                    dest_buf->TakeBuffer(f->vc);
                    _last_vc[n][subnet][c] = f->vc;
                }

                _last_class[n][subnet] = c;

                _partial_packets[n][c].pop_front();

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
                    *gWatchOut << GetSimTime() << " | "
                               << "node" << n << " | "
                               << "Injecting flit " << f->id
                               << " into subnet " << subnet
                               << " at time " << _time
                               << " with priority " << f->pri
                               << "." << endl;
                }
                f->itime = _time;

                // Pass VC "back"
                if(!_partial_packets[n][c].empty() && !f->tail) {
                    Flit * const nf = _partial_packets[n][c].front();
                    nf->vc = f->vc;
                }

                if((_sim_state == warming_up) || (_sim_state == running)) {
                    ++_sent_flits[c][n];
                    if(f->head) {
                        ++_sent_packets[c][n];
                    }
                }

#ifdef TRACK_FLOWS
                ++_injected_flits[c][n];
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
            }
        }
        flits[subnet].clear();
        /* ==== Power Gate - Begin ==== */
        _net[subnet]->PowerStateEvaluate();
        /* ==== Power Gate - End ==== */
        _net[subnet]->Evaluate( );
        _net[subnet]->WriteOutputs( );
    }

    ++_monitor_counter;
    ++_time;
    assert(_time);
    if(gTrace){
        cout<<"TIME "<<_time<<endl;
    }

}

void FLOVTrafficManager::_ClearStats( )
{
    _slowest_flit.assign(_classes, -1);
    _slowest_packet.assign(_classes, -1);

    for ( int c = 0; c < _classes; ++c ) {

        _plat_stats[c]->Clear( );
        _nlat_stats[c]->Clear( );
        _flat_stats[c]->Clear( );

        _frag_stats[c]->Clear( );

        _sent_packets[c].assign(_nodes, 0);
        _accepted_packets[c].assign(_nodes, 0);
        _sent_flits[c].assign(_nodes, 0);
        _accepted_flits[c].assign(_nodes, 0);

#ifdef TRACK_STALLS
        _buffer_busy_stalls[c].assign(_subnets*_routers, 0);
        _buffer_conflict_stalls[c].assign(_subnets*_routers, 0);
        _buffer_full_stalls[c].assign(_subnets*_routers, 0);
        _buffer_reserved_stalls[c].assign(_subnets*_routers, 0);
        _crossbar_conflict_stalls[c].assign(_subnets*_routers, 0);
#endif
        if(_pair_stats){
            for ( int i = 0; i < _nodes; ++i ) {
                for ( int j = 0; j < _nodes; ++j ) {
                    _pair_plat[c][i*_nodes+j]->Clear( );
                    _pair_nlat[c][i*_nodes+j]->Clear( );
                    _pair_flat[c][i*_nodes+j]->Clear( );
                }
            }
        }
        _hop_stats[c]->Clear();
        /* ==== Power Gate - Begin ==== */
        _flov_hop_stats[c]->Clear();
        /* ==== Power Gate - End ==== */

    }

    _reset_time = _time;
}


void FLOVTrafficManager::_UpdateOverallStats() {
    TrafficManager::_UpdateOverallStats();

    for ( int c = 0; c < _classes; ++c ) {

        if(_measure_stats[c] == 0) {
            continue;
        }

       /* ==== Power Gate - Begin ==== */
        _overall_flov_hop_stats[c] += _flov_hop_stats[c]->Average();
        /* ==== Power Gate - End ==== */
    }
}

void FLOVTrafficManager::WriteStats(ostream & os) const {

    os << "%=================================" << endl;

    for(int c = 0; c < _classes; ++c) {

        if(_measure_stats[c] == 0) {
            continue;
        }

        //c+1 due to matlab array starting at 1
        os << "plat(" << c+1 << ") = " << _plat_stats[c]->Average() << ";" << endl
           << "plat_hist(" << c+1 << ",:) = " << *_plat_stats[c] << ";" << endl
           << "nlat(" << c+1 << ") = " << _nlat_stats[c]->Average() << ";" << endl
           << "nlat_hist(" << c+1 << ",:) = " << *_nlat_stats[c] << ";" << endl
           << "flat(" << c+1 << ") = " << _flat_stats[c]->Average() << ";" << endl
           << "flat_hist(" << c+1 << ",:) = " << *_flat_stats[c] << ";" << endl
           << "frag_hist(" << c+1 << ",:) = " << *_frag_stats[c] << ";" << endl
           << "hops(" << c+1 << ",:) = " << *_hop_stats[c] << ";" << endl
           /* ==== Power Gate - Begin ==== */
           << "flov hops(" << c+1 << ",:)" << *_flov_hop_stats[c] << ";" << endl;
           /* ==== Power Gate - End ==== */
        if(_pair_stats){
            os<< "pair_sent(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_plat[c][i*_nodes+j]->NumSamples() << " ";
                }
            }
            os << "];" << endl
               << "pair_plat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_plat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
            os << "];" << endl
               << "pair_nlat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_nlat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
            os << "];" << endl
               << "pair_flat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_flat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
        }

        double time_delta = (double)(_drain_time - _reset_time);

        os << "];" << endl
           << "sent_packets(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_packets[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "accepted_packets(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_packets[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "sent_flits(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_flits[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "accepted_flits(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_flits[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "sent_packet_size(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_flits[c][d] / (double)_sent_packets[c][d] << " ";
        }
        os << "];" << endl
           << "accepted_packet_size(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_flits[c][d] / (double)_accepted_packets[c][d] << " ";
        }
        os << "];" << endl;
#ifdef TRACK_STALLS
        os << "buffer_busy_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_busy_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_conflict_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_conflict_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_full_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_full_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_reserved_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_reserved_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "crossbar_conflict_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_crossbar_conflict_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl;
#endif
    }
}

void FLOVTrafficManager::DisplayOverallStats( ostream & os ) const {

    os << "====== Overall Traffic Statistics ======" << endl;
    for ( int c = 0; c < _classes; ++c ) {

        if(_measure_stats[c] == 0) {
            continue;
        }

        os << "====== Traffic class " << c << " ======" << endl;

        os << "Packet latency average = " << _overall_avg_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Network latency average = " << _overall_avg_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Flit latency average = " << _overall_avg_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Fragmentation average = " << _overall_avg_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Injected packet rate average = " << _overall_avg_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Accepted packet rate average = " << _overall_avg_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Injected flit rate average = " << _overall_avg_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Accepted flit rate average = " << _overall_avg_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Injected packet size average = " << _overall_avg_sent[c] / _overall_avg_sent_packets[c]
           << " (" << _total_sims << " samples)" << endl;

        os << "Accepted packet size average = " << _overall_avg_accepted[c] / _overall_avg_accepted_packets[c]
           << " (" << _total_sims << " samples)" << endl;

        os << "Hops average = " << _overall_hop_stats[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "FLOV hops average = " << _overall_flov_hop_stats[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

#ifdef TRACK_STALLS
        os << "Buffer busy stall rate = " << (double)_overall_buffer_busy_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer conflict stall rate = " << (double)_overall_buffer_conflict_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer full stall rate = " << (double)_overall_buffer_full_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer reserved stall rate = " << (double)_overall_buffer_reserved_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Crossbar conflict stall rate = " << (double)_overall_crossbar_conflict_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
#endif

    }

}

