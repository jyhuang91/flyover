#include <sstream>
#include <cmath>
#include <fstream>
#include <limits>

#include "mem/ruby/network/booksim2/booksim.hh"
#include "mem/ruby/network/booksim2/gem5nordtrafficmanager.hh"
#include "mem/ruby/network/booksim2/random_utils.hh"
#include "mem/ruby/network/booksim2/networks/gem5net.hh"
#include "mem/ruby/slicc_interface/NetworkMessage.hh"
#include "mem/ruby/network/Network.hh"
#include "mem/ruby/network/MessageBuffer.hh"
#include "sim/clocked_object.hh"

#define REPORT_INTERVAL 100000

Gem5NoRDTrafficManager::Gem5NoRDTrafficManager(const Configuration &config, const
        vector<BSNetwork *> &net, int vnets)
  : Gem5TrafficManager(config, net, vnets)
{
    _powergate_type = config.GetStr("powergate_type");

    assert(config.GetInt("wait_for_tail_credit") > 0);

    // reset VC buffer depth for bypass latches
    vector<bool> const & router_states = _net[0]->GetRouterStates();
    for (int n = 0; n < _nodes; ++n) {
        int r = Gem5Net::NodeToRouter(n);
        if (router_states[r] == false) {
            for (int subnet = 0; subnet < _subnets; ++subnet) {
                _buf_states[n][subnet]->SetVCBufferSize(1);
            }
        }
    }

    _buffers.resize(_nodes);
    _bypass_routers.resize(router_states.size(), false);
    _bypass_nodes.resize(_nodes, false);
    for (int n = 0; n < _nodes; ++n) {
        _buffers[n].resize(_subnets);
        int r = Gem5Net::NodeToRouter(n);
        for (int subnet = 0; subnet < _subnets; ++subnet) {
            ostringstream tmp_name;
            if (router_states[r] == true) {
                tmp_name << "terminal_buf_" << n << "_" << subnet;
            } else {
                _bypass_nodes[n] = true;
                _bypass_routers[r] = true;
                tmp_name << "bypass_buf_" << n << "_" << subnet;
            }
            _buffers[n][subnet] = new Buffer(config, 1, this, tmp_name.str());
        }
    }

    _routing_deadlock_timeout_threshold = config.GetInt("routing_deadlock_timeout_threshold");

    _performance_centric_wakeup_threshold = config.GetInt("nord_performance_centric_wakeup_threshold");
    _power_centric_wakeup_threshold = config.GetInt("nord_power_centric_wakeup_threshold");
    _wakeup_monitor_epoch = config.GetInt("nord_wakeup_monitor_epoch");
    _last_monitor_epoch = 0;
    _wakeup_monitor_vc_requests.resize(_net[0]->NumRouters(), 0);
    _performance_centric_routers.resize(_net[0]->NumRouters(), false);
    for (int n = 0; n < gNodes; ++n) {
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
}

Gem5NoRDTrafficManager::~Gem5NoRDTrafficManager()
{
}

void Gem5NoRDTrafficManager::_GeneratePacket(int source, int stype, int vnet, uint64_t time)
{
    int cl = 0;

    MsgPtr msg_ptr = _input_buffer[source][vnet]->peekMsgPtr();
    NetworkMessage *net_msg_ptr = safe_cast<NetworkMessage *>(msg_ptr.get());
    NetDest net_msg_dest = net_msg_ptr->getInternalDestination();

    // get all the destinations associated with this message
    vector<NodeID> dest_nodes = net_msg_dest.getAllDest();

    // bytes
    int size = (int) ceil((double) _net_ptr->MessageSizeType_to_int(
                net_msg_ptr->getMessageSize())*8 / _flit_size);

    for (int ctr = 0; ctr < dest_nodes.size(); ctr++) {
        Flit::FlitType packet_type = Flit::ANY_TYPE;

        int packet_dest = dest_nodes[ctr];
        bool record = false;

        if ((packet_dest < 0) || (packet_dest >= _nodes)) {
            ostringstream err;
            err << "Incorrect packet destination " << packet_dest
                << " for stype " << packet_type << "!" << endl;
            Error(err.str());
        }

        record = true;

        int subnetwork = ((packet_type == Flit::ANY_TYPE) ?
                RandomInt(_subnets-1) :
                _subnet[packet_type]);

        bool watch = gWatchOut && (_packets_to_watch.count(_cur_pid) > 0);

        // for debugging
        watch = watch | _watch_all_pkts;

        if (watch) {
            *gWatchOut << GetSimTime() << " | "
                << "node " << source << " | "
                << "Enqueuing packet " << _cur_pid
                << " at time " << time
                << " through router " << Gem5Net::NodeToRouter(source)
                << " (to node " << packet_dest
                << " attached to router " << Gem5Net::NodeToRouter(packet_dest)
                << ")." << endl;
        }

        MsgPtr new_msg_ptr = msg_ptr->clone();
        int pid = _cur_pid++;

        for (int i = 0; i < size; i++) {
            Flit * f = Flit::New();
            f->id = _cur_id++;
            assert(_cur_id);
            f->pid = pid;
            f->watch = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
            f->subnetwork = subnetwork;
            f->src = source;
            f->ctime = time;
            f->record = record;
            f->cl = cl;
            f->src_router = Gem5Net::NodeToRouter(source);
            f->gem5_vnet = vnet;
            //f->vc = vnet; // why assign it?
            f->msg_ptr = new_msg_ptr;

            _total_in_flight_flits[f->cl].insert(make_pair(f->id, f));
            if (record) {
                _measured_in_flight_flits[f->cl].insert(make_pair(f->id, f));
            }

            if (gTrace) {
                cout << "New Flit " << f->src << endl;
            }

            if (i == 0) { // Head flit
                f->head = true;
                // packets are only generated to nodes smaller or equal to limit
                f->dest = packet_dest;
                f->dest_router = Gem5Net::NodeToRouter(packet_dest);
                const vector<Router *> routers = _net[0]->GetRouters();
                f->ring_dest = routers[f->dest_router]->GetRingID();
            } else {
                f->head = false;
                f->dest = -1;
                f->dest_router = -1;
            }
            switch (_pri_type) {
            case class_based:
                f->pri = cl;
                assert(f->pri >= 0);
                break;
            case age_based:
                f->pri = numeric_limits<int>::max() - time % numeric_limits<int>::max();
                assert(f->pri >= 0);
                break;
            case sequence_based:
                f->pri = numeric_limits<int>::max() - _packet_seq_no[source];
                assert(f->pri >= 0);
                break;
            default:
                f->pri = 0;
            }
            if (i == (size - 1)) { // Tail flit
                f->tail = true;
            } else {
                f->tail = false;
            }

            f->vc = -1;

            if (f->watch) {
                *gWatchOut << GetSimTime() << " | "
                      << "node " << source << " | "
                      << "Enqueueing flit " << f->id
                      << " (packet " << f->pid
                      << ") through router " << f->src_router
                      << " at time " << time
                      << "." << endl;
            }

            _partial_packets[source][cl].push_back(f);
        }
        assert(_cur_pid);
    }
}

void Gem5NoRDTrafficManager::Step()
{
    uint64_t prev_time = _time;
    _time = _net_ptr->curCycle();
    if (_time > prev_time + 1) {
        vector<Router *> routers = _net[0]->GetRouters();
        for (int r = 0; r < routers.size(); r++) {
            routers[r]->SynchronizeCycle(_time - prev_time - 1);
        }
    }

    bool flits_in_flight = false;
    for (int c = 0; c < _classes; c++) {
        flits_in_flight |= !_total_in_flight_flits[c].empty();
    }
    if (flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)) {
        _deadlock_timer = 0;
        cout << "WARNING: Possible network deadlock.\n";
        /* ==== Power Gate Debug - Begin ==== */
        cout << GetSimTime() << endl;
        const vector<Router *> routers = _net[0]->GetRouters();
        for (int n = 0; n < routers.size(); ++n) {
            if (n % gK == 0) {
                cout << endl;
            }
            cout << Router::POWERSTATE[routers[n]->GetPowerState()] << "\t";
        }
        cout << endl;
        for (int n = 0; n < routers.size(); ++n)
            routers[n]->Display(cout);
        cout << endl << endl;
        /* ==== Power Gate Debug - End ==== */
    }

    const vector<Router *> routers = _net[0]->GetRouters();
    if (_performance_centric_wakeup_threshold > 0 &&
            _power_centric_wakeup_threshold > 0) {
        for (int n = 0; n < _bypass_routers.size(); ++n) {
            int wakeup_threshold = _performance_centric_routers[n] ?
                _performance_centric_wakeup_threshold :
                _power_centric_wakeup_threshold;
            if (_wakeup_monitor_vc_requests[n] > wakeup_threshold &&
                    routers[n]->GetPowerState() == Router::power_on) {
                assert(_bypass_routers[n] == true);
                _wakeup_monitor_vc_requests[n] = 0;
                // fake off node won't inject flits, only bypass
            }
        }
    }

    vector<map<int, Flit *> > flits(_subnets);

    for (int subnet = 0; subnet < _subnets; subnet++) {
        for (int n = 0; n < _nodes; n++) {
            Flit * const f = _net[subnet]->ReadFlit(n);
            if (f) {
                if (f->watch) {
                    *gWatchOut << GetSimTime() << " | "
                          << "node" << n << " | "
                          << "Ejecting flit " << f->id
                          << " (packet " << f->pid << ")"
                          << " through router " << f->dest_router
                          << " from VC " << f->vc
                          << "." << endl;
                }
                flits[subnet].insert(make_pair(n, f));

                if (!_bypass_nodes[n]) {
                    NetworkMessage *net_msg_ptr =
                        safe_cast<NetworkMessage *>(f->msg_ptr.get());
                    bool is_data = _net_ptr->isDataMsg(
                            net_msg_ptr->getMessageSize());
                    if (is_data) {
                        _net_ptr->increment_received_data_flits(f->gem5_vnet);
                    } else {
                        _net_ptr->increment_received_ctrl_flits(f->gem5_vnet);
                    }
                    _accepted_flits[f->cl][n]++;
                    if (f->tail) {
                        if (is_data) {
                            _net_ptr->increment_received_data_pkts(f->gem5_vnet);
                        } else {
                            _net_ptr->increment_received_ctrl_pkts(f->gem5_vnet);
                        }
                        _accepted_packets[f->cl][n]++;
                    }
                }
            }

            Credit * const c = _net[subnet]->ReadCredit(n);
            if (c) {
#ifdef TRACK_FLOWS
#endif
                _buf_states[n][subnet]->ProcessCredit(c);
                c->Free();
            }
        }
        _net[subnet]->ReadInputs();
    }

    _Inject();

    for (int subnet = 0; subnet < _subnets; subnet++) {
        for (int n = 0; n < _nodes; n++) {

            Flit * f = nullptr;
            BufferState * const dest_buf = _buf_states[n][subnet];

            int const last_class = _last_class[n][subnet];
            int class_limit = _classes;

            int const last_vc = _last_vc[n][subnet][last_class];
            int vc_limit = _vcs;
            int bypass_vc = -1;

            if (_bypass_nodes[n] == false) {
                if (_hold_switch_for_packet) {
                    list<Flit *> const & pp = _partial_packets[n][last_class];
                    if (!pp.empty() && !pp.front()->head &&
                            !dest_buf->IsFullFor(pp.front()->vc)) {
                        f = pp.front();
                        assert(f->vc == _last_vc[n][subnet][last_class]);

                        // if we're holding the connection, we don't need to check that class
                        // again in the for loop
                        class_limit--;
                    }
                }

                for (int i = 1; i <= class_limit; i++) {

                    int const c = (last_class + i) % _classes;

                    list<Flit *> const & pp = _partial_packets[n][c];

                    if (pp.empty()) {
                        continue;
                    }

                    Flit * const cf = pp.front();
                    assert(cf);
                    assert(cf->cl == c);

                    if (cf->subnetwork != subnet) {
                        continue;
                    }

                    if (f && (f->pri >= cf->pri)) {
                        continue;
                    }

                    if (cf->head && cf->vc == -1) { // Find first available VC

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
                        if (cf->watch) {
                            *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                << "Finding output VC for flit " << cf->id
                                << ":" << endl;
                        }
                        for (int i = 1; i <= vc_count; i++) {
                            int const lvc = _last_vc[n][subnet][c];
                            int const vc = (lvc < vc_start || lvc > vc_end) ?
                                vc_start : (vc_start + (lvc - vc_start + i) % vc_count);
                            assert((vc >= vc_start) && (vc <= vc_end));
                            if (!dest_buf->IsAvailableFor(vc)) {
                                if (cf->watch) {
                                    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                        << "  Output VC " << vc << " is busy." << endl;
                                }
                            } else {
                                if (dest_buf->IsFullFor(vc)) {
                                    if (cf->watch) {
                                        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                            << "  Output VC " << vc << " is full." << endl;
                                    }
                                } else {
                                    if (cf->watch) {
                                        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                            << "  Selected output VC " << vc << "." << endl;
                                    }
                                    cf->vc = vc;
                                    break;
                                }
                            }
                        }
                    }

                    if (cf->vc == -1) {
                        if (cf->watch) {
                            *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                << "No output VC found for flit " << cf->id
                                << "." << endl;
                        }
                    } else {
                        if (dest_buf->IsFullFor(cf->vc)) {
                            if (cf->watch) {
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
                                            << " router " << Gem5Net::NodeToRouter(n)
                                            << " | flit " << f->id << " is selected for hold switch bypass"
                                            << endl;
                                    }
                                    f->vc = out_vc;
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

                    if (f && (f->pri >= cf->pri)) {
                        assert(!f->head);
                        assert(f->vc != -1);
                        assert(_buffers[n][subnet]->GetState(bypass_vc) == VC::active);
                        continue;
                    }

                    if (_buffers[n][subnet]->GetState(vc) == VC::vc_alloc) { // Find first available VC

                        assert(cf->head);

                        const vector<Router *> routers = _net[0]->GetRouters();
                        int const r = Gem5Net::NodeToRouter(n);

                        if (_performance_centric_wakeup_threshold > 0 &&
                                _power_centric_wakeup_threshold > 0) {
                            int wakeup_threshold = _performance_centric_routers[r] ?
                                _performance_centric_wakeup_threshold :
                                _power_centric_wakeup_threshold;
                            ++_wakeup_monitor_vc_requests[r];
                            if (_time / _wakeup_monitor_epoch > _last_monitor_epoch) {
                                if (_wakeup_monitor_vc_requests[r] >= wakeup_threshold) {
                                    routers[r]->WakeUp();
                                }
                            }
                        }

                        OutputSet route_set;
                        _rf(routers[r], cf, -1, &route_set, true);
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
                    } else {
                        assert(_buffers[n][subnet]->GetState(vc) == VC::active);
                        if(dest_buf->IsFullFor(cf->vc)) {
                            if(cf->watch) {
                                *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                    << "Selected output VC " << cf->vc
                                    << " is full for flit " << cf->id
                                    << "." << endl;
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

            if (f) {

                assert(f->subnetwork == subnet);

                int const c = f->cl;

                if (f->head) {

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

                if (_bypass_nodes[n] == true) {
                    _buffers[n][subnet]->RemoveFlit(bypass_vc);
                } else {
                    _partial_packets[n][c].pop_front();
                }

#ifdef TRACK_FLOWS
#endif

                dest_buf->SendingFlit(f);

                if (_pri_type == network_age_based) {
                    f->pri = numeric_limits<int>::max() - _time % numeric_limits<int>::max();
                    assert(f->pri >= 0);
                }

                if (f->watch) {
                    if (f->src == n)
                        *gWatchOut << GetSimTime() << " | "
                            << "node" << n << " | "
                            << "Injecting flit " << f->id
                            << " into subnet " << subnet
                            << " at time " << _time
                            << " with priority " << f->pri
                            << "." << endl;
                    else
                        *gWatchOut << GetSimTime() << " | "
                            << "node" << n << " | "
                            << "Bypassing flit " << f->id
                            << " into subnet " << subnet
                            << " through router " << Gem5Net::NodeToRouter(n)
                            << " to VC " << f->vc
                            << " at time " << _time
                            << " with priority " << f->pri
                            << "." << endl;
                }

                if (f->src == n) {
                    f->itime = _time;

                    // Pass VC "back"
                    if (!_partial_packets[n][c].empty() && !f->tail) {
                        Flit * const nf = _partial_packets[n][c].front();
                        nf->vc = f->vc;
                    }

                    NetworkMessage *net_msg_ptr =
                        safe_cast<NetworkMessage *>(f->msg_ptr.get());
                    bool is_data = _net_ptr->isDataMsg(
                            net_msg_ptr->getMessageSize());
                    if (is_data) {
                        _net_ptr->increment_injected_data_flits(f->gem5_vnet);
                    } else {
                        _net_ptr->increment_injected_ctrl_flits(f->gem5_vnet);
                    }
                    _sent_flits[c][n]++;
                    if (f->head) {
                        if (is_data) {
                            _net_ptr->increment_injected_data_pkts(f->gem5_vnet);
                        } else {
                            _net_ptr->increment_injected_ctrl_pkts(f->gem5_vnet);
                        }
                        _sent_packets[c][n]++;
                    }
                } else {
                    assert(_bypass_nodes[n]);

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

                    if (f->tail) {
                        _buffers[n][subnet]->SetState(bypass_vc, VC::idle);
                    }
                }

#ifdef TRACK_FLOW
#endif
                _net[subnet]->WriteFlit(f, n);
            }
        }
    }

    for (int subnet = 0; subnet < _subnets; subnet++) {
        for (int n = 0; n < _nodes; n++) {
            map<int, Flit *>::const_iterator iter = flits[subnet].find(n);
            if (iter != flits[subnet].end()) {
                Flit * const f = iter->second;

                if (!_bypass_nodes[n]) {
                    f->atime = _time;
                    if (f->watch) {
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
#endif
                    _RetireFlit(f, n);
                } else {

                    f->rtime = _time;
                    if (f->watch) {
                        *gWatchOut << GetSimTime() << " | "
                            << "node" << n << " | "
                            << "receives bypass flit " << f->id
                            << " (pid: " << f->pid
                            << ") and buffer at VC " << f->vc
                            << " through router " << Gem5Net::NodeToRouter(n)
                            << "." << endl;
                    }

                    int vc = f->vc;
                    f->bypass_vc = f->vc;
                    f->vc = -1;
                    assert(_buffers[n][subnet]->Empty(vc));
                    _buffers[n][subnet]->AddFlit(vc, f);
                    if (f->head) {
                        assert(_buffers[n][subnet]->GetState(vc) == VC::idle);
                        _buffers[n][subnet]->SetState(vc, VC::vc_alloc);
                    }
                }
            }
        }
        flits[subnet].clear();
        /* ==== Power Gate - Begin ==== */
        _net[subnet]->PowerStateEvaluate();
        /* ==== Power Gate - End ==== */
        _net[subnet]->Evaluate();
        _net[subnet]->WriteOutputs();
    }

    _network_time++;

    //if (_time > _next_report) {
    //    while (_time > _next_report) {
    //        cout << "Booksim report System time " << _time
    //            << "\tnetwork time " << _network_time << endl;
    //        _next_report += REPORT_INTERVAL;
    //    }
    //    DisplayStats(cout);
    //}


    if (_time / _wakeup_monitor_epoch > _last_monitor_epoch) {
        _last_monitor_epoch = _time / _wakeup_monitor_epoch;
    }
    assert(_network_time);
    assert(_time);
    if (gTrace) {
        cout << "TIME " << _time << endl;
    }
}

int Gem5NoRDTrafficManager::NextPowerEventCycle()
{
    int cycle = 0;
    const vector<Router *> routers = _net[0]->GetRouters();
    for (int r = 0; r < routers.size(); r++) {
        int next_event_cycle = routers[r]->NextPowerEventCycle();
        if (cycle > 0 && next_event_cycle > 0 && next_event_cycle < cycle)
            cycle = next_event_cycle;
    }

    return cycle;
}
