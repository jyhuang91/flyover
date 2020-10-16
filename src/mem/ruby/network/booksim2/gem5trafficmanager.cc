#include <sstream>
#include <cmath>
#include <fstream>
#include <limits>
//#include <cstdlib>
//#include <ctime>

#include "mem/ruby/network/booksim2/booksim.hh"
#include "mem/ruby/network/booksim2/gem5trafficmanager.hh"
#include "mem/ruby/network/booksim2/gem5flovtrafficmanager.hh"
#include "mem/ruby/network/booksim2/gem5nordtrafficmanager.hh"
#include "mem/ruby/network/booksim2/random_utils.hh"
#include "mem/ruby/network/booksim2/networks/gem5net.hh"
#include "mem/ruby/slicc_interface/NetworkMessage.hh"
#include "mem/ruby/network/Network.hh"
#include "mem/ruby/network/MessageBuffer.hh"
#include "mem/ruby/common/Global.hh"
#include "sim/clocked_object.hh"

#define REPORT_INTERVAL 100000

Gem5TrafficManager *Gem5TrafficManager::New(Configuration const &config,
        vector<BSNetwork *> const &net, int vnets)
{
    Gem5TrafficManager *result = nullptr;
    string sim_type = config.GetStr("sim_type");
    if (sim_type == "default" || sim_type == "rp") {
        result = new Gem5TrafficManager(config, net, vnets);
    } else if (sim_type == "flov") {
        result = new Gem5FLOVTrafficManager(config, net, vnets);
    } else if (sim_type == "nord") {
        result = new Gem5NoRDTrafficManager(config, net, vnets);
    } else {
        cerr << "Unknown simulation type: " << sim_type << endl;
    }
    return result;
}

Gem5TrafficManager::Gem5TrafficManager(const Configuration &config, const
        vector<BSNetwork *> &net, int vnets)
  : TrafficManager(config, net)
{
    _vnets = vnets;
    _last_vnet.resize(_nodes, 0);
    _flit_size = config.GetInt("channel_width");
    _network_time = 0;
    _next_report = REPORT_INTERVAL;
    _watch_all_pkts = (config.GetInt("watch_all_pkts") > 0);
    _outdir = config.GetStr("outdir");
    _stats_dumped = 0;

    _sim_state = running;
}

Gem5TrafficManager::~Gem5TrafficManager()
{
}

void Gem5TrafficManager::_RetireFlit(Flit *f, int dest)
{
    _deadlock_timer = 0;

    // send to the output message buffer
    assert(f);
    if (f->tail) {
        _output_buffer[dest][f->gem5_vnet]->enqueue(
                f->msg_ptr, Cycles(1));
        if (f->watch) {
            *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                << *(_output_buffer[dest][f->gem5_vnet])
                << " consumes the packet " << f->pid << "." << endl;
        }
    }

    _net_ptr->increment_flat(Cycles(f->atime - f->itime), f->gem5_vnet);

    if (f->tail) {

        _net_ptr->increment_hops(f->hops, f->gem5_vnet);
        _net_ptr->increment_flov_hops(f->flov_hops, f->gem5_vnet);

        Flit * head;
        if (f->head) {
            head = f;
        } else {
            map<int, Flit *>::iterator iter =
                _retired_packets[f->cl].find(f->pid);
            head = iter->second;
        }

        _net_ptr->increment_plat(Cycles(f->atime - head->ctime), f->gem5_vnet);
        _net_ptr->increment_nlat(Cycles(f->atime - head->itime), f->gem5_vnet);
        _net_ptr->increment_frag(
                Cycles((f->atime - head->atime) - (f->id - head->id)),
                f->gem5_vnet);
        _net_ptr->increment_qlat(
                Cycles(head->ctime - _net_ptr->ticksToCycles(head->msg_ptr->getTime())),
                head->gem5_vnet);
    }

    TrafficManager::_RetireFlit(f, dest);
}

void Gem5TrafficManager::_GeneratePacket(int source, int stype, int vnet, uint64_t time)
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

void Gem5TrafficManager::_Inject()
{
    for (int input = 0; input < _input_buffer.size(); input++) {
        if (_partial_packets[input][0].empty()) {

            int const last_vnet = _last_vnet[input];

            for (int v = 1; v <= _vnets; v++) {

                int vnet = (last_vnet + v) % _vnets;
                if (_input_buffer[input][vnet] == nullptr) {
                    continue;
                }

                if (_input_buffer[input][vnet]->isReady()) {
                    _GeneratePacket(input, 1, vnet, _time);
                    if (_watch_all_pkts) {
                        *gWatchOut << GetSimTime() << " | " << FullName()
                            << " | " << *(_input_buffer[input][vnet])
                            << " generate new packets." << endl;
                    }
                    _input_buffer[input][vnet]->dequeue();
                    _last_vnet[input] = vnet;
                }
            }
        } else {
            //??
        }
    }
}

void Gem5TrafficManager::Step()
{
    uint64_t prev_time = _time;
    _time = _net_ptr->curCycle();
    if (_time > prev_time + 1) {
        vector<BSRouter *> routers = _net[0]->GetRouters();
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
        const vector<BSRouter *> routers = _net[0]->GetRouters();
        for (int n = 0; n < routers.size(); ++n) {
            if (n % gK == 0) {
                cout << endl;
            }
            cout << BSRouter::POWERSTATE[routers[n]->GetPowerState()] << "\t";
        }
        cout << endl;
        for (int n = 0; n < routers.size(); ++n)
            routers[n]->Display(cout);
        cout << endl << endl;
        /* ==== Power Gate Debug - End ==== */
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
                    if (_noq) {
                        assert(_lookahead_routing);
                        const FlitChannel * inject = _net[subnet]->GetInject(n);
                        const BSRouter * router = inject->GetSink();
                        assert(router);
                        int in_channel = inject->GetSinkPort();

                        // NOTE: Because the lookahead is not for injection, but for the
                        // first hop, we have to temporarily set cf's VC to be non-negative
                        // in order to avoid setting of an assertion in the routing
                        cf->vc = vc_start;
                        _rf(router, cf, in_channel, &cf->la_route_set, false);
                        cf->vc = -1;

                        if (cf->watch) {
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

            if (f) {

                 assert(f->subnetwork == subnet);

                 int const c = f->cl;

                 if (f->head) {

                     if (_lookahead_routing) {
                         if (!_noq) {
                             const FlitChannel * inject = _net[subnet]->GetInject(n);
                             const BSRouter * router = inject->GetSink();
                             assert(router);
                             int in_channel = inject->GetSinkPort();
                             _rf(router, f, in_channel, &f->la_route_set, false);
                             if (f->watch) {
                                 *gWatchOut << GetSimTime() << " | "
                                      << "node" << n << " | "
                                      << "Generating lookahead routing info for flit " << f->id
                                      << "." << endl;
                             }
                         } else if (f->watch) {
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
#endif

                 dest_buf->SendingFlit(f);

                 if (_pri_type == network_age_based) {
                     f->pri = numeric_limits<int>::max() - _time % numeric_limits<int>::max();
                     assert(f->pri >= 0);
                 }

                 if (f->watch) {
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

    assert(_network_time);
    assert(_time);
    if (gTrace) {
        cout << "TIME " << _time << endl;
    }
}

void Gem5TrafficManager::RegisterMessageBuffers(vector<vector<MessageBuffer *> >& in,
                                                vector<vector<MessageBuffer *> >& out)
{
    _input_buffer = in;
    _output_buffer = out;
}

bool Gem5TrafficManager::functionalRead(Packet *pkt)
{
    for (int subnet = 0; subnet < _subnets; subnet++) {
        if (_net[subnet]->functionalRead(pkt))
            return true;
    }

    return false;
}

uint32_t Gem5TrafficManager::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;

    for (int subnet = 0; subnet < _subnets; subnet++) {
        num_functional_writes += _net[subnet]->functionalWrite(pkt);
    }

    return num_functional_writes;
}

bool Gem5TrafficManager::EventsOutstanding()
{
    bool have_events = Flit::OutStanding() || Credit::OutStanding() ||
        Handshake::OutStanding() || RouterPowerStateTransition();

    return have_events;
}

bool Gem5TrafficManager::RouterPowerStateTransition()
{
    for (int subnet = 0; subnet < _subnets; subnet++) {
        const vector<BSRouter *> routers = _net[subnet]->GetRouters();
        for (int r = 0; r < routers.size(); r++) {
            BSRouter::ePowerState ps = routers[r]->GetPowerState();
            if (ps == BSRouter::draining || ps == BSRouter::wakeup)
                return true;
            if (ps == BSRouter::power_off && routers[r]->GetWakeUpSignal())
                return true;
        }
    }

    return false;
}

void Gem5TrafficManager::DisplayOverallStats(ostream& os) const
{
    os << "====== BookSim Overall Traffic Statistics ======" << endl;
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

void Gem5TrafficManager::ResetStats()
{
    vector<BSRouter *> routers = _net[0]->GetRouters();
    for (int r = 0; r < routers.size(); r++) {
        routers[r]->ResetStats();
    }

    _ClearStats();
}

void Gem5TrafficManager::DumpStats()
{
    string stat_file = string("/booksimstats");
    if (_stats_dumped)
        stat_file += to_string(_stats_dumped) + string(".json");
    else
        stat_file += string(".json");

    _stats_dumped++;

    uint64_t cycles = _time - g_ruby_start;
    cout << "Total cycles: " << cycles
        << " (start: " << g_ruby_start << ", end: " << _time << ")" << endl;

    _UpdateOverallStats();
    DisplayOverallStats(cout);

    const vector<BSRouter *> routers = _net[0]->GetRouters();

    string outfile = _outdir + stat_file;
    ofstream statsout(outfile.c_str(), ofstream::out);

    statsout << "{" << endl;
    statsout << "    \"start\": " << g_ruby_start << "," << endl;
    statsout << "    \"end\": " << _time << "," << endl;
    statsout << "    \"cycles\": " << cycles << "," << endl;
    statsout << "    \"routers\": {" << endl;
    for (int r = 0; r < routers.size(); r++) {
        uint64_t power_off_cycles = routers[r]->GetPowerOffCycles();
        double power_off_percentile = (double) power_off_cycles / (double) cycles;
        uint64_t reads = routers[r]->GetBufferReads();
        uint64_t writes = routers[r]->GetBufferWrites();
        uint64_t activities = routers[r]->GetSwitchActivities();
        statsout << "        \"router_" << r << "\": {" << endl;
        statsout << "            \"inputs\": " << routers[r]->NumInputs() << "," << endl;
        statsout << "            \"outputs\": " << routers[r]->NumOutputs() << "," << endl;
        statsout << "            \"reads\": " << reads << "," << endl;
        statsout << "            \"writes\": " << writes << "," << endl;
        statsout << "            \"switches\": " << activities << "," << endl;
        statsout << "            \"power-off-cycles\": " << power_off_cycles << "," << endl;
        statsout << "            \"power-off-percentile\": " << power_off_percentile << "," << endl;
        statsout << "            \"power-on-percentile\": " << 1.0 - power_off_percentile << endl;
        if (r < routers.size() - 1)
            statsout << "        }," << endl;
        else
            statsout << "        }" << endl;
    }
    statsout << "    }," << endl;
    statsout << "    \"packet-latency\": {" << endl;
    statsout << "        \"average\": " << _overall_avg_plat[0] << "," << endl;
    statsout << "        \"minimum\": " << _overall_min_plat[0] << "," << endl;
    statsout << "        \"maximum\": " << _overall_max_plat[0] << endl;
    statsout << "    }," << endl;
    statsout << "    \"network-latency\": {" << endl;
    statsout << "        \"average\": " << _overall_avg_nlat[0] << "," << endl;
    statsout << "        \"minimum\": " << _overall_min_nlat[0] << "," << endl;
    statsout << "        \"maximum\": " << _overall_max_nlat[0] << endl;
    statsout << "    }," << endl;
    statsout << "    \"flit-latency\": {" << endl;
    statsout << "        \"average\": " << _overall_avg_flat[0] << "," << endl;
    statsout << "        \"minimum\": " << _overall_min_flat[0] << "," << endl;
    statsout << "        \"maximum\": " << _overall_max_flat[0] << endl;
    statsout << "    }," << endl;
    statsout << "    \"fragmentation\": {" << endl;
    statsout << "        \"average\": " << _overall_avg_frag[0] << "," << endl;
    statsout << "        \"minimum\": " << _overall_min_frag[0] << "," << endl;
    statsout << "        \"maximum\": " << _overall_max_frag[0] << endl;
    statsout << "    }," << endl;
    statsout << "    \"injected-packet-rate\": {" << endl;
    statsout << "        \"average\": " << _overall_avg_sent_packets[0] << "," << endl;
    statsout << "        \"minimum\": " << _overall_min_sent_packets[0] << "," << endl;
    statsout << "        \"maximum\": " << _overall_max_sent_packets[0] << endl;
    statsout << "    }," << endl;
    statsout << "    \"accepted-packet-rate\": {" << endl;
    statsout << "        \"average\": " << _overall_avg_accepted_packets[0] << "," << endl;
    statsout << "        \"minimum\": " << _overall_min_accepted_packets[0] << "," << endl;
    statsout << "        \"maximum\": " << _overall_max_accepted_packets[0] << endl;
    statsout << "    }," << endl;
    statsout << "    \"injected-flit-rate\": {" << endl;
    statsout << "        \"average\": " << _overall_avg_sent[0] << "," << endl;
    statsout << "        \"minimum\": " << _overall_min_sent[0] << "," << endl;
    statsout << "        \"maximum\": " << _overall_max_sent[0] << endl;
    statsout << "    }," << endl;
    statsout << "    \"accepted-flit-rate\": {" << endl;
    statsout << "        \"average\": " << _overall_avg_accepted[0] << "," << endl;
    statsout << "        \"minimum\": " << _overall_min_accepted[0] << "," << endl;
    statsout << "        \"maximum\": " << _overall_max_accepted[0] << endl;
    statsout << "    }," << endl;
    statsout << "    \"injected-packet-size-average\": " << _overall_avg_sent[0] / _overall_avg_sent_packets[0] << "," << endl;
    statsout << "    \"accpeted-packet-size-average\": " << _overall_avg_accepted[0] / _overall_avg_accepted_packets[0] << "," << endl;
    statsout << "    \"hops-average\": " << _overall_hop_stats[0] << endl;
    statsout << "}" << endl;

    statsout.close();
}

