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

//int FLOVTrafficManager::_IssuePacket( int source, int cl )
//{
//    int result = 0;
//    if(_use_read_write[cl]){ //use read and write
//        //check queue for waiting replies.
//        //check to make sure it is on time yet
//        if (!_repliesPending[source].empty()) {
//            if(_repliesPending[source].front()->time <= _time) {
//                result = -1;
//            }
//        } else {
//      
//            //produce a packet
//            if(_injection_process[cl]->test(source)) {
//	
//                //coin toss to determine request type.
//                result = (RandomFloat() < _write_fraction[cl]) ? 2 : 1;
//	
//                _requestsOutstanding[source]++;
//            }
//        }
//    } else { //normal mode
//        result = _injection_process[cl]->test(source) ? 1 : 0;
//        _requestsOutstanding[source]++;
//    } 
//    if(result != 0) {
//        _packet_seq_no[source]++;
//    }
//    return result;
//}

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
    if (_traffic[cl] == "tornado") {
        for (int i = 1; i < _nodes; ++i) {
            packet_destination = _traffic_pattern[cl]->dest((source+i)%_nodes);
            if (core_states[packet_destination] == true)
                break;
        }
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
                        /* ==== Power Gate - Begin ==== */
                        int i = 0;
                        if (_traffic[c] == "tornado") {
                            for (i = 0; i < _nodes; ++i) {
                                int pkt_dest = _traffic_pattern[c]->dest((input+i)%_nodes);
                                if (core_states[pkt_dest] == true)
                                    break;
                            }
                        }
                        if (i == _nodes)
                            break;
                        /* ==== Power Gate - End ==== */
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

    ++_time;
    assert(_time);
    if(gTrace){
        cout<<"TIME "<<_time<<endl;
    }

}
  
//bool FLOVTrafficManager::_PacketsOutstanding( ) const
//{
//    for ( int c = 0; c < _classes; ++c ) {
//        if ( _measure_stats[c] ) {
//            if ( _measured_in_flight_flits[c].empty() ) {
//	
//                for ( int s = 0; s < _nodes; ++s ) {
//                    if ( !_qdrained[s][c] ) {
//#ifdef DEBUG_DRAIN
//                        cout << "waiting on queue " << s << " class " << c;
//                        cout << ", time = " << _time << " qtime = " << _qtime[s][c] << endl;
//#endif
//                        return true;
//                    }
//                }
//            } else {
//#ifdef DEBUG_DRAIN
//                cout << "in flight = " << _measured_in_flight_flits[c].size() << endl;
//#endif
//                return true;
//            }
//        }
//    }
//    return false;
//}

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

//void FLOVTrafficManager::_ComputeStats( const vector<int> & stats, int *sum, int *min, int *max, int *min_pos, int *max_pos ) const 
//{
//    int const count = stats.size();
//    assert(count > 0);
//
//    if(min_pos) {
//        *min_pos = 0;
//    }
//    if(max_pos) {
//        *max_pos = 0;
//    }
//
//    if(min) {
//        *min = stats[0];
//    }
//    if(max) {
//        *max = stats[0];
//    }
//
//    *sum = stats[0];
//
//    for ( int i = 1; i < count; ++i ) {
//        int curr = stats[i];
//        if ( min  && ( curr < *min ) ) {
//            *min = curr;
//            if ( min_pos ) {
//                *min_pos = i;
//            }
//        }
//        if ( max && ( curr > *max ) ) {
//            *max = curr;
//            if ( max_pos ) {
//                *max_pos = i;
//            }
//        }
//        *sum += curr;
//    }
//}

//void FLOVTrafficManager::_DisplayRemaining( ostream & os ) const 
//{
//    for(int c = 0; c < _classes; ++c) {
//
//        map<int, Flit *>::const_iterator iter;
//        int i;
//
//        os << "Class " << c << ":" << endl;
//
//        os << "Remaining flits: ";
//        for ( iter = _total_in_flight_flits[c].begin( ), i = 0;
//              ( iter != _total_in_flight_flits[c].end( ) ) && ( i < 10 );
//              iter++, i++ ) {
//            os << iter->first << " ";
//        }
//        if(_total_in_flight_flits[c].size() > 10)
//            os << "[...] ";
//    
//        os << "(" << _total_in_flight_flits[c].size() << " flits)" << endl;
//    
//        os << "Measured flits: ";
//        for ( iter = _measured_in_flight_flits[c].begin( ), i = 0;
//              ( iter != _measured_in_flight_flits[c].end( ) ) && ( i < 10 );
//              iter++, i++ ) {
//            os << iter->first << " ";
//        }
//        if(_measured_in_flight_flits[c].size() > 10)
//            os << "[...] ";
//    
//        os << "(" << _measured_in_flight_flits[c].size() << " flits)" << endl;
//    
//    }
//}

//bool FLOVTrafficManager::_SingleSim( )
//{
//    int converged = 0;
//  
//    //once warmed up, we require 3 converging runs to end the simulation 
//    vector<double> prev_latency(_classes, 0.0);
//    vector<double> prev_accepted(_classes, 0.0);
//    bool clear_last = false;
//    int total_phases = 0;
//    while( ( total_phases < _max_samples ) && 
//           ( ( _sim_state != running ) || 
//             ( converged < 3 ) ) ) {
//    
//        if ( clear_last || (( ( _sim_state == warming_up ) && ( ( total_phases % 2 ) == 0 ) )) ) {
//            clear_last = false;
//            _ClearStats( );
//        }
//    
//    
//        for ( int iter = 0; iter < _sample_period; ++iter )
//            _Step( );
//    
//        //cout << _sim_state << endl;
//
//        UpdateStats();
//        DisplayStats();
//    
//        int lat_exc_class = -1;
//        int lat_chg_exc_class = -1;
//        int acc_chg_exc_class = -1;
//    
//        for(int c = 0; c < _classes; ++c) {
//      
//            if(_measure_stats[c] == 0) {
//                continue;
//            }
//
//            double cur_latency = _plat_stats[c]->Average( );
//
//            int total_accepted_count;
//            _ComputeStats( _accepted_flits[c], &total_accepted_count );
//            double total_accepted_rate = (double)total_accepted_count / (double)(_time - _reset_time);
//            double cur_accepted = total_accepted_rate / (double)_nodes;
//
//            double latency_change = fabs((cur_latency - prev_latency[c]) / cur_latency);
//            prev_latency[c] = cur_latency;
//
//            double accepted_change = fabs((cur_accepted - prev_accepted[c]) / cur_accepted);
//            prev_accepted[c] = cur_accepted;
//
//            double latency = (double)_plat_stats[c]->Sum();
//            double count = (double)_plat_stats[c]->NumSamples();
//      
//            map<int, Flit *>::const_iterator iter;
//            for(iter = _total_in_flight_flits[c].begin(); 
//                iter != _total_in_flight_flits[c].end(); 
//                iter++) {
//                latency += (double)(_time - iter->second->ctime);
//                count++;
//            }
//      
//            if((lat_exc_class < 0) &&
//               (_latency_thres[c] >= 0.0) &&
//               ((latency / count) > _latency_thres[c])) {
//                lat_exc_class = c;
//            }
//      
//            cout << "latency change    = " << latency_change << endl;
//            if(lat_chg_exc_class < 0) {
//                if((_sim_state == warming_up) &&
//                   (_warmup_threshold[c] >= 0.0) &&
//                   (latency_change > _warmup_threshold[c])) {
//                    lat_chg_exc_class = c;
//                } else if((_sim_state == running) &&
//                          (_stopping_threshold[c] >= 0.0) &&
//                          (latency_change > _stopping_threshold[c])) {
//                    lat_chg_exc_class = c;
//                }
//            }
//      
//            cout << "throughput change = " << accepted_change << endl;
//            if(acc_chg_exc_class < 0) {
//                if((_sim_state == warming_up) &&
//                   (_acc_warmup_threshold[c] >= 0.0) &&
//                   (accepted_change > _acc_warmup_threshold[c])) {
//                    acc_chg_exc_class = c;
//                } else if((_sim_state == running) &&
//                          (_acc_stopping_threshold[c] >= 0.0) &&
//                          (accepted_change > _acc_stopping_threshold[c])) {
//                    acc_chg_exc_class = c;
//                }
//            }
//      
//        }
//    
//        // Fail safe for latency mode, throughput will ust continue
//        if ( _measure_latency && ( lat_exc_class >= 0 ) ) {
//      
//            cout << "Average latency for class " << lat_exc_class
//                << " exceeded " << _latency_thres[lat_exc_class]
//                << " cycles. Aborting simulation." << endl;
//            converged = 0; 
//            _sim_state = draining;
//            _drain_time = _time;
//            if(_stats_out) {
//                WriteStats(*_stats_out);
//            }
//            break;
//      
//        }
//    
//        if ( _sim_state == warming_up ) {
//            if ( ( _warmup_periods > 0 ) ? 
//                 ( total_phases + 1 >= _warmup_periods ) :
//                 ( ( !_measure_latency || ( lat_chg_exc_class < 0 ) ) &&
//                   ( acc_chg_exc_class < 0 ) ) ) {
//                cout << "Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
//                clear_last = true;
//                _sim_state = running;
//            }
//        } else if(_sim_state == running) {
//            if ( ( !_measure_latency || ( lat_chg_exc_class < 0 ) ) &&
//                 ( acc_chg_exc_class < 0 ) ) {
//                ++converged;
//            } else {
//                converged = 0;
//            }
//        }
//        ++total_phases;
//    }
//  
//    if ( _sim_state == running ) {
//        ++converged;
//    
//        _sim_state  = draining;
//        _drain_time = _time;
//
//        if ( _measure_latency ) {
//            cout << "Draining all recorded packets ..." << endl;
//            int empty_steps = 0;
//            while( _PacketsOutstanding( ) ) { 
//                _Step( ); 
//	
//                ++empty_steps;
//	
//                if ( empty_steps % 1000 == 0 ) {
//	  
//                    int lat_exc_class = -1;
//	  
//                    for(int c = 0; c < _classes; c++) {
//	    
//                        double threshold = _latency_thres[c];
//	    
//                        if(threshold < 0.0) {
//                            continue;
//                        }
//	    
//                        double acc_latency = _plat_stats[c]->Sum();
//                        double acc_count = (double)_plat_stats[c]->NumSamples();
//	    
//                        map<int, Flit *>::const_iterator iter;
//                        for(iter = _total_in_flight_flits[c].begin(); 
//                            iter != _total_in_flight_flits[c].end(); 
//                            iter++) {
//                            acc_latency += (double)(_time - iter->second->ctime);
//                            acc_count++;
//                        }
//	    
//                        if((acc_latency / acc_count) > threshold) {
//                            lat_exc_class = c;
//                            break;
//                        }
//                    }
//	  
//                    if(lat_exc_class >= 0) {
//                        cout << "Average latency for class " << lat_exc_class
//                            << " exceeded " << _latency_thres[lat_exc_class]
//                            << " cycles. Aborting simulation." << endl;
//                        converged = 0; 
//                        _sim_state = warming_up;
//                        if(_stats_out) {
//                            WriteStats(*_stats_out);
//                        }
//                        break;
//                    }
//	  
//                    _DisplayRemaining( ); 
//	  
//                }
//            }
//        }
//    } else {
//        cout << "Too many sample periods needed to converge" << endl;
//    }
//  
//    return ( converged > 0 );
//}

//bool FLOVTrafficManager::Run( )
//{
//    for ( int sim = 0; sim < _total_sims; ++sim ) {
//
//        _time = 0;
//
//        //remove any pending request from the previous simulations
//        _requestsOutstanding.assign(_nodes, 0);
//        for (int i=0;i<_nodes;i++) {
//            while(!_repliesPending[i].empty()) {
//                _repliesPending[i].front()->Free();
//                _repliesPending[i].pop_front();
//            }
//        }
//
//        //reset queuetime for all sources
//        for ( int s = 0; s < _nodes; ++s ) {
//            _qtime[s].assign(_classes, 0);
//            _qdrained[s].assign(_classes, false);
//        }
//
//        // warm-up ...
//        // reset stats, all packets after warmup_time marked
//        // converge
//        // draing, wait until all packets finish
//        _sim_state    = warming_up;
//  
//        _ClearStats( );
//
//        for(int c = 0; c < _classes; ++c) {
//            _traffic_pattern[c]->reset();
//            _injection_process[c]->reset();
//        }
//
//        if ( !_SingleSim( ) ) {
//            cout << "Simulation unstable, ending ..." << endl;
//            return false;
//        }
//
//        // Empty any remaining packets
//        cout << "Draining remaining packets ..." << endl;
//        _empty_network = true;
//        int empty_steps = 0;
//
//        bool packets_left = false;
//        for(int c = 0; c < _classes; ++c) {
//            packets_left |= !_total_in_flight_flits[c].empty();
//        }
//
//        while( packets_left ) { 
//            _Step( ); 
//
//            ++empty_steps;
//
//            if ( empty_steps % 1000 == 0 ) {
//                _DisplayRemaining( ); 
//            }
//      
//            packets_left = false;
//            for(int c = 0; c < _classes; ++c) {
//                packets_left |= !_total_in_flight_flits[c].empty();
//            }
//        }
//        //wait until all the credits are drained as well
//        while(Credit::OutStanding()!=0){
//            _Step();
//        }
//        _empty_network = false;
//
//        //for the love of god don't ever say "Time taken" anywhere else
//        //the power script depend on it
//        cout << "Time taken is " << _time << " cycles" <<endl; 
//
//        if(_stats_out) {
//            WriteStats(*_stats_out);
//        }
//        _UpdateOverallStats();
//    }
//  
//    DisplayOverallStats();
//    if(_print_csv_results) {
//        DisplayOverallStatsCSV();
//    }
//  
//    return true;
//}

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

//void FLOVTrafficManager::UpdateStats() {
//#if defined(TRACK_FLOWS) || defined(TRACK_STALLS)
//    for(int c = 0; c < _classes; ++c) {
//#ifdef TRACK_FLOWS
//        {
//            char trail_char = (c == _classes - 1) ? '\n' : ',';
//            if(_injected_flits_out) *_injected_flits_out << _injected_flits[c] << trail_char;
//            _injected_flits[c].assign(_nodes, 0);
//            if(_ejected_flits_out) *_ejected_flits_out << _ejected_flits[c] << trail_char;
//            _ejected_flits[c].assign(_nodes, 0);
//        }
//#endif
//        for(int subnet = 0; subnet < _subnets; ++subnet) {
//#ifdef TRACK_FLOWS
//            if(_outstanding_credits_out) *_outstanding_credits_out << _outstanding_credits[c][subnet] << ',';
//            if(_stored_flits_out) *_stored_flits_out << vector<int>(_nodes, 0) << ',';
//#endif
//            for(int router = 0; router < _routers; ++router) {
//                Router * const r = _router[subnet][router];
//#ifdef TRACK_FLOWS
//                char trail_char = 
//                    ((router == _routers - 1) && (subnet == _subnets - 1) && (c == _classes - 1)) ? '\n' : ',';
//                if(_received_flits_out) *_received_flits_out << r->GetReceivedFlits(c) << trail_char;
//                if(_stored_flits_out) *_stored_flits_out << r->GetStoredFlits(c) << trail_char;
//                if(_sent_flits_out) *_sent_flits_out << r->GetSentFlits(c) << trail_char;
//                if(_outstanding_credits_out) *_outstanding_credits_out << r->GetOutstandingCredits(c) << trail_char;
//                if(_active_packets_out) *_active_packets_out << r->GetActivePackets(c) << trail_char;
//                r->ResetFlowStats(c);
//#endif
//#ifdef TRACK_STALLS
//                _buffer_busy_stalls[c][subnet*_routers+router] += r->GetBufferBusyStalls(c);
//                _buffer_conflict_stalls[c][subnet*_routers+router] += r->GetBufferConflictStalls(c);
//                _buffer_full_stalls[c][subnet*_routers+router] += r->GetBufferFullStalls(c);
//                _buffer_reserved_stalls[c][subnet*_routers+router] += r->GetBufferReservedStalls(c);
//                _crossbar_conflict_stalls[c][subnet*_routers+router] += r->GetCrossbarConflictStalls(c);
//                r->ResetStallStats(c);
//#endif
//            }
//        }
//    }
//#ifdef TRACK_FLOWS
//    if(_injected_flits_out) *_injected_flits_out << flush;
//    if(_received_flits_out) *_received_flits_out << flush;
//    if(_stored_flits_out) *_stored_flits_out << flush;
//    if(_sent_flits_out) *_sent_flits_out << flush;
//    if(_outstanding_credits_out) *_outstanding_credits_out << flush;
//    if(_ejected_flits_out) *_ejected_flits_out << flush;
//    if(_active_packets_out) *_active_packets_out << flush;
//#endif
//#endif
//
//#ifdef TRACK_CREDITS
//    for(int s = 0; s < _subnets; ++s) {
//        for(int n = 0; n < _nodes; ++n) {
//            BufferState const * const bs = _buf_states[n][s];
//            for(int v = 0; v < _vcs; ++v) {
//                if(_used_credits_out) *_used_credits_out << bs->OccupancyFor(v) << ',';
//                if(_free_credits_out) *_free_credits_out << bs->AvailableFor(v) << ',';
//                if(_max_credits_out) *_max_credits_out << bs->LimitFor(v) << ',';
//            }
//        }
//        for(int r = 0; r < _routers; ++r) {
//            Router const * const rtr = _router[s][r];
//            char trail_char = 
//                ((r == _routers - 1) && (s == _subnets - 1)) ? '\n' : ',';
//            if(_used_credits_out) *_used_credits_out << rtr->UsedCredits() << trail_char;
//            if(_free_credits_out) *_free_credits_out << rtr->FreeCredits() << trail_char;
//            if(_max_credits_out) *_max_credits_out << rtr->MaxCredits() << trail_char;
//        }
//    }
//    if(_used_credits_out) *_used_credits_out << flush;
//    if(_free_credits_out) *_free_credits_out << flush;
//    if(_max_credits_out) *_max_credits_out << flush;
//#endif
//
//}

//void FLOVTrafficManager::DisplayStats(ostream & os) const {
//  
//    for(int c = 0; c < _classes; ++c) {
//    
//        if(_measure_stats[c] == 0) {
//            continue;
//        }
//    
//        cout << "Class " << c << ":" << endl;
//    
//        cout 
//            << "Packet latency average = " << _plat_stats[c]->Average() << endl
//            << "\tminimum = " << _plat_stats[c]->Min() << endl
//            << "\tmaximum = " << _plat_stats[c]->Max() << endl
//            << "Network latency average = " << _nlat_stats[c]->Average() << endl
//            << "\tminimum = " << _nlat_stats[c]->Min() << endl
//            << "\tmaximum = " << _nlat_stats[c]->Max() << endl
//            << "Slowest packet = " << _slowest_packet[c] << endl
//            << "Flit latency average = " << _flat_stats[c]->Average() << endl
//            << "\tminimum = " << _flat_stats[c]->Min() << endl
//            << "\tmaximum = " << _flat_stats[c]->Max() << endl
//            << "Slowest flit = " << _slowest_flit[c] << endl
//            << "Fragmentation average = " << _frag_stats[c]->Average() << endl
//            << "\tminimum = " << _frag_stats[c]->Min() << endl
//            << "\tmaximum = " << _frag_stats[c]->Max() << endl;
//    
//        int count_sum, count_min, count_max;
//        double rate_sum, rate_min, rate_max;
//        double rate_avg;
//        int sent_packets, sent_flits, accepted_packets, accepted_flits;
//        int min_pos, max_pos;
//        double time_delta = (double)(_time - _reset_time);
//        _ComputeStats(_sent_packets[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
//        rate_sum = (double)count_sum / time_delta;
//        rate_min = (double)count_min / time_delta;
//        rate_max = (double)count_max / time_delta;
//        rate_avg = rate_sum / (double)_nodes;
//        sent_packets = count_sum;
//        cout << "Injected packet rate average = " << rate_avg << endl
//             << "\tminimum = " << rate_min 
//             << " (at node " << min_pos << ")" << endl
//             << "\tmaximum = " << rate_max
//             << " (at node " << max_pos << ")" << endl;
//        _ComputeStats(_accepted_packets[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
//        rate_sum = (double)count_sum / time_delta;
//        rate_min = (double)count_min / time_delta;
//        rate_max = (double)count_max / time_delta;
//        rate_avg = rate_sum / (double)_nodes;
//        accepted_packets = count_sum;
//        cout << "Accepted packet rate average = " << rate_avg << endl
//             << "\tminimum = " << rate_min 
//             << " (at node " << min_pos << ")" << endl
//             << "\tmaximum = " << rate_max
//             << " (at node " << max_pos << ")" << endl;
//        _ComputeStats(_sent_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
//        rate_sum = (double)count_sum / time_delta;
//        rate_min = (double)count_min / time_delta;
//        rate_max = (double)count_max / time_delta;
//        rate_avg = rate_sum / (double)_nodes;
//        sent_flits = count_sum;
//        cout << "Injected flit rate average = " << rate_avg << endl
//             << "\tminimum = " << rate_min 
//             << " (at node " << min_pos << ")" << endl
//             << "\tmaximum = " << rate_max
//             << " (at node " << max_pos << ")" << endl;
//        _ComputeStats(_accepted_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
//        rate_sum = (double)count_sum / time_delta;
//        rate_min = (double)count_min / time_delta;
//        rate_max = (double)count_max / time_delta;
//        rate_avg = rate_sum / (double)_nodes;
//        accepted_flits = count_sum;
//        cout << "Accepted flit rate average= " << rate_avg << endl
//             << "\tminimum = " << rate_min 
//             << " (at node " << min_pos << ")" << endl
//             << "\tmaximum = " << rate_max
//             << " (at node " << max_pos << ")" << endl;
//    
//        cout << "Injected packet length average = " << (double)sent_flits / (double)sent_packets << endl
//             << "Accepted packet length average = " << (double)accepted_flits / (double)accepted_packets << endl;
//
//        cout << "Total in-flight flits = " << _total_in_flight_flits[c].size()
//             << " (" << _measured_in_flight_flits[c].size() << " measured)"
//             << endl;
//    
//#ifdef TRACK_STALLS
//        _ComputeStats(_buffer_busy_stalls[c], &count_sum);
//        rate_sum = (double)count_sum / time_delta;
//        rate_avg = rate_sum / (double)(_subnets*_routers);
//        os << "Buffer busy stall rate = " << rate_avg << endl;
//        _ComputeStats(_buffer_conflict_stalls[c], &count_sum);
//        rate_sum = (double)count_sum / time_delta;
//        rate_avg = rate_sum / (double)(_subnets*_routers);
//        os << "Buffer conflict stall rate = " << rate_avg << endl;
//        _ComputeStats(_buffer_full_stalls[c], &count_sum);
//        rate_sum = (double)count_sum / time_delta;
//        rate_avg = rate_sum / (double)(_subnets*_routers);
//        os << "Buffer full stall rate = " << rate_avg << endl;
//        _ComputeStats(_buffer_reserved_stalls[c], &count_sum);
//        rate_sum = (double)count_sum / time_delta;
//        rate_avg = rate_sum / (double)(_subnets*_routers);
//        os << "Buffer reserved stall rate = " << rate_avg << endl;
//        _ComputeStats(_crossbar_conflict_stalls[c], &count_sum);
//        rate_sum = (double)count_sum / time_delta;
//        rate_avg = rate_sum / (double)(_subnets*_routers);
//        os << "Crossbar conflict stall rate = " << rate_avg << endl;
//#endif
//    
//    }
//}

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

//string FLOVTrafficManager::_OverallStatsCSV(int c) const
//{
//    ostringstream os;
//    os << _traffic[c]
//       << ',' << _use_read_write[c]
//       << ',' << _load[c]
//       << ',' << _overall_min_plat[c] / (double)_total_sims
//       << ',' << _overall_avg_plat[c] / (double)_total_sims
//       << ',' << _overall_max_plat[c] / (double)_total_sims
//       << ',' << _overall_min_nlat[c] / (double)_total_sims
//       << ',' << _overall_avg_nlat[c] / (double)_total_sims
//       << ',' << _overall_max_nlat[c] / (double)_total_sims
//       << ',' << _overall_min_flat[c] / (double)_total_sims
//       << ',' << _overall_avg_flat[c] / (double)_total_sims
//       << ',' << _overall_max_flat[c] / (double)_total_sims
//       << ',' << _overall_min_frag[c] / (double)_total_sims
//       << ',' << _overall_avg_frag[c] / (double)_total_sims
//       << ',' << _overall_max_frag[c] / (double)_total_sims
//       << ',' << _overall_min_sent_packets[c] / (double)_total_sims
//       << ',' << _overall_avg_sent_packets[c] / (double)_total_sims
//       << ',' << _overall_max_sent_packets[c] / (double)_total_sims
//       << ',' << _overall_min_accepted_packets[c] / (double)_total_sims
//       << ',' << _overall_avg_accepted_packets[c] / (double)_total_sims
//       << ',' << _overall_max_accepted_packets[c] / (double)_total_sims
//       << ',' << _overall_min_sent[c] / (double)_total_sims
//       << ',' << _overall_avg_sent[c] / (double)_total_sims
//       << ',' << _overall_max_sent[c] / (double)_total_sims
//       << ',' << _overall_min_accepted[c] / (double)_total_sims
//       << ',' << _overall_avg_accepted[c] / (double)_total_sims
//       << ',' << _overall_max_accepted[c] / (double)_total_sims
//       << ',' << _overall_avg_sent[c] / _overall_avg_sent_packets[c]
//       << ',' << _overall_avg_accepted[c] / _overall_avg_accepted_packets[c]
//       << ',' << _overall_hop_stats[c] / (double)_total_sims;
//
//#ifdef TRACK_STALLS
//    os << ',' << (double)_overall_buffer_busy_stalls[c] / (double)_total_sims
//       << ',' << (double)_overall_buffer_conflict_stalls[c] / (double)_total_sims
//       << ',' << (double)_overall_buffer_full_stalls[c] / (double)_total_sims
//       << ',' << (double)_overall_buffer_reserved_stalls[c] / (double)_total_sims
//       << ',' << (double)_overall_crossbar_conflict_stalls[c] / (double)_total_sims;
//#endif
//
//    return os.str();
//}

//void FLOVTrafficManager::DisplayOverallStatsCSV(ostream & os) const {
//    for(int c = 0; c < _classes; ++c) {
//        os << "results:" << c << ',' << _OverallStatsCSV() << endl;
//    }
//}

//read the watchlist
//void FLOVTrafficManager::_LoadWatchList(const string & filename){
//    ifstream watch_list;
//    watch_list.open(filename.c_str());
//  
//    string line;
//    if(watch_list.is_open()) {
//        while(!watch_list.eof()) {
//            getline(watch_list, line);
//            if(line != "") {
//                if(line[0] == 'p') {
//                    _packets_to_watch.insert(atoi(line.c_str()+1));
//                } else {
//                    _flits_to_watch.insert(atoi(line.c_str()));
//                }
//            }
//        }
//    
//    } else {
//        Error("Unable to open flit watch file: " + filename);
//    }
//}

//int FLOVTrafficManager::_GetNextPacketSize(int cl) const
//{
//    assert(cl >= 0 && cl < _classes);
//
//    vector<int> const & psize = _packet_size[cl];
//    int sizes = psize.size();
//
//    if(sizes == 1) {
//        return psize[0];
//    }
//
//    vector<int> const & prate = _packet_size_rate[cl];
//    int max_val = _packet_size_max_val[cl];
//
//    int pct = RandomInt(max_val);
//
//    for(int i = 0; i < (sizes - 1); ++i) {
//        int const limit = prate[i];
//        if(limit > pct) {
//            return psize[i];
//        } else {
//            pct -= limit;
//        }
//    }
//    assert(prate.back() > pct);
//    return psize.back();
//}

//double FLOVTrafficManager::_GetAveragePacketSize(int cl) const
//{
//    vector<int> const & psize = _packet_size[cl];
//    int sizes = psize.size();
//    if(sizes == 1) {
//        return (double)psize[0];
//    }
//    vector<int> const & prate = _packet_size_rate[cl];
//    int sum = 0;
//    for(int i = 0; i < sizes; ++i) {
//        sum += psize[i] * prate[i];
//    }
//    return (double)sum / (double)(_packet_size_max_val[cl] + 1);
//}
