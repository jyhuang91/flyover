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

/*network.cpp
 *
 *This class is the basis of the entire network, it contains, all the routers
 *channels in the network, and is extended by all the network topologies
 *
 */

#include <cassert>
#include <sstream>
#include <algorithm>

#include "booksim.hpp"
#include "network.hpp"
#include "random_utils.hpp"

#include "kncube.hpp"
#include "fly.hpp"
#include "cmesh.hpp"
#include "flatfly_onchip.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
#include "anynet.hpp"
#include "dragonfly.hpp"


/* ==== DSENT power model - Begin ==== */
netEnergyStats::netEnergyStats() { Reset(); }

netEnergyStats::~netEnergyStats() {}

void netEnergyStats::Reset() {
  link_dynamic_energy = 0;
  link_leakage_energy = 0;

  buf_rd_energy = 0;
  buf_wt_energy = 0;
  buf_leakage_energy = 0;

  sw_dynamic_energy = 0;
  sw_leakage_energy = 0;

  xbar_dynamic_energy = 0;
  xbar_leakage_energy = 0;

  clk_dynamic_energy = 0;
  clk_leakage_energy = 0;

  power_gate_energy = 0;

  tot_rt_dynamic_energy = 0;
  tot_rt_leakage_energy = 0;
  tot_rt_energy = 0;

  tot_net_dynamic_energy = 0;
  tot_net_leakage_energy = 0;
  tot_net_energy = 0;

  tot_time = 0;
}

void netEnergyStats::AddEnergy(netEnergyStats &stat) {
  link_dynamic_energy += stat.link_dynamic_energy;
  link_leakage_energy += stat.link_leakage_energy;
  buf_rd_energy += stat.buf_rd_energy;
  buf_wt_energy += stat.buf_wt_energy;
  buf_leakage_energy += stat.buf_leakage_energy;
  sw_dynamic_energy += stat.sw_dynamic_energy;
  sw_leakage_energy += stat.sw_leakage_energy;
  xbar_dynamic_energy += stat.xbar_dynamic_energy;
  xbar_leakage_energy += stat.xbar_leakage_energy;
  clk_dynamic_energy += stat.clk_dynamic_energy;
  clk_leakage_energy += stat.clk_leakage_energy;
  power_gate_energy += stat.power_gate_energy;

  tot_rt_dynamic_energy += stat.tot_rt_dynamic_energy;
  tot_rt_leakage_energy += stat.tot_rt_leakage_energy;
  tot_rt_energy += stat.tot_rt_energy;

  tot_net_dynamic_energy += stat.tot_net_dynamic_energy;
  tot_net_leakage_energy += stat.tot_net_leakage_energy;
  tot_net_energy += stat.tot_net_energy;

  tot_time += stat.tot_time;
}

void netEnergyStats::ComputeTotalEnergy() {
  tot_rt_dynamic_energy = buf_rd_energy + buf_wt_energy + sw_dynamic_energy +
                          xbar_dynamic_energy + clk_dynamic_energy;
  tot_rt_leakage_energy = buf_leakage_energy + sw_leakage_energy +
                          xbar_leakage_energy + clk_leakage_energy;
  tot_rt_energy =
      tot_rt_dynamic_energy + tot_rt_leakage_energy + power_gate_energy;

  tot_net_dynamic_energy = tot_rt_dynamic_energy + link_dynamic_energy;
  tot_net_leakage_energy = tot_rt_leakage_energy + link_leakage_energy;
  tot_net_energy =
      tot_net_dynamic_energy + tot_net_leakage_energy + power_gate_energy;
}
/* ==== DSENT power model - End ==== */

Network::Network( const Configuration &config, const string & name ) :
  TimedModule( 0, name )
{
  _size     = -1; 
  _nodes    = -1; 
  _channels = -1;
  _classes  = config.GetInt("classes");
  /* ==== DSENT power model - Begin ==== */
  _net_energy_stats.Reset();
  /* ==== DSENT power model - End ==== */
  /* ==== Power Gate - Begin ==== */
  _fabric_manager = config.GetInt("fabric_manager");
  string type = config.GetStr("sim_type");
  //assert((_fabric_manager >= 0 && type == "rp") || _fabric_manager < 0);
  _powergate_auto_config = config.GetInt("powergate_auto_config") > 0;
  _powergate_type = config.GetStr("powergate_type");
  _off_cores = config.GetIntArray("off_cores");
  _off_routers = config.GetIntArray("off_routers");
  _powergate_seed = config.GetInt("powergate_seed");
  _powergate_percentile = config.GetInt("powergate_percentile");
  /* ==== Power Gate - End ==== */
}

Network::~Network( )
{
  for ( int r = 0; r < _size; ++r ) {
    if ( _routers[r] ) delete _routers[r];
  }
  for ( int s = 0; s < _nodes; ++s ) {
    if ( _inject[s] ) delete _inject[s];
    if ( _inject_cred[s] ) delete _inject_cred[s];
  }
  for ( int d = 0; d < _nodes; ++d ) {
    if ( _eject[d] ) delete _eject[d];
    if ( _eject_cred[d] ) delete _eject_cred[d];
  }
  for ( int c = 0; c < _channels; ++c ) {
    if ( _chan[c] ) delete _chan[c];
    if ( _chan_cred[c] ) delete _chan_cred[c];
    /* ==== Power Gate - Begin ==== */
    if ( _chan_handshake[c] ) delete _chan_handshake[c];
    /* ==== Power Gate - End ==== */
  }
}

Network * Network::New(const Configuration & config, const string & name)
{
  const string topo = config.GetStr( "topology" );
  Network * n = NULL;
  if ( topo == "torus" ) {
    KNCube::RegisterRoutingFunctions() ;
    n = new KNCube( config, name, false );
  } else if ( topo == "mesh" ) {
    KNCube::RegisterRoutingFunctions() ;
    n = new KNCube( config, name, true );
  } else if ( topo == "cmesh" ) {
    CMesh::RegisterRoutingFunctions() ;
    n = new CMesh( config, name );
  } else if ( topo == "fly" ) {
    KNFly::RegisterRoutingFunctions() ;
    n = new KNFly( config, name );
  } else if ( topo == "qtree" ) {
    QTree::RegisterRoutingFunctions() ;
    n = new QTree( config, name );
  } else if ( topo == "tree4" ) {
    Tree4::RegisterRoutingFunctions() ;
    n = new Tree4( config, name );
  } else if ( topo == "fattree" ) {
    FatTree::RegisterRoutingFunctions() ;
    n = new FatTree( config, name );
  } else if ( topo == "flatfly" ) {
    FlatFlyOnChip::RegisterRoutingFunctions() ;
    n = new FlatFlyOnChip( config, name );
  } else if ( topo == "anynet"){
    AnyNet::RegisterRoutingFunctions() ;
    n = new AnyNet(config, name);
  } else if ( topo == "dragonflynew"){
    DragonFlyNew::RegisterRoutingFunctions() ;
    n = new DragonFlyNew(config, name);
  } else {
    cerr << "Unknown topology: " << topo << endl;
  }
  
  /*legacy code that insert random faults in the networks
   *not sure how to use this
   */
  if ( n && ( config.GetInt( "link_failures" ) > 0 ) ) {
    n->InsertRandomFaults( config );
  }
  return n;
}

void Network::_Alloc( )
{
  assert( ( _size != -1 ) && 
	  ( _nodes != -1 ) && 
	  ( _channels != -1 ) );

  _routers.resize(_size);
  gNodes = _nodes;

  /* ==== Power Gate - Begin ==== */
  _core_states.resize(_size, true);
  _router_states.resize(_size, true);
  if (_powergate_auto_config) {
    _off_cores.clear();
    _off_routers.clear();
    // random off core id generation for core parking
    unsigned num_off_cores = _nodes * _powergate_percentile / 100;
    if (num_off_cores + gK > _nodes) {
      ostringstream err;
      err << "percentile of power-gating is too high, should keep one row active" << endl;
      Error(err.str());
    }
    if (_fabric_manager < 0 || _fabric_manager >= _size) {
      _fabric_manager = gK*gK/2 - gK/2 - 1;
    }
    assert(_fabric_manager < _size);
    RandomSeed(_powergate_seed);
    for (unsigned i = 0; i < num_off_cores; ++i) {
      int cid = RandomInt(_nodes - 1 - gK);
      while (find(_off_cores.begin(), _off_cores.end(), cid) != _off_cores.end() ||
          cid == _fabric_manager) {
        cid = RandomInt(_nodes - 1 - gK);
      }
      _off_cores.push_back(cid);
    }
    assert(_off_cores.size() == num_off_cores);
    sort(_off_cores.begin(), _off_cores.end());
    for (unsigned i = 0; i < num_off_cores; i++) {
      int cid = _off_cores[i];
      _core_states[cid] = false;
    }
#ifdef DEBUG_POWERGATE_CONFIG
    cout << "random auto generated off cores (seed = " << _powergate_seed << "):" << endl << "\t";
    for (unsigned i = 0; i < num_off_cores; i++) {
      cout << _off_cores[i] << ", ";
    }
    cout << endl;
#endif
    // router power states
    if (_powergate_type == "flov" || _powergate_type == "rflov") {
      _router_states = _core_states;
    } else if (_powergate_type == "rpa") {  // aggressive RP
      _router_states = _core_states;
      // initialize adjacent matrix
      vector<vector<int> > adj_mat;
      adj_mat.resize(_size);
      for (int i = 0; i < _size; ++i) {
        adj_mat[i].resize(_size, -1);

        int ix = i % gK;
        int iy = i / gK;
        for (int j = 0; j < _size; ++j) {
          if (_router_states[i] == false || _router_states[j] == false)
            continue;

          int jx = j % gK;
          int jy = j / gK;

          if (i == j) {
            adj_mat[i][j] = 0;
          } else if ( (abs(ix - jx) == 1 && iy == jy) ||
              (abs(iy - jy) == 1 && ix == jx) ) {
            adj_mat[i][j] = 1;
          }
        }
      }
#ifdef DEBUG_POWERGATE_CONFIG
      cout << "Adjacent List: " << endl;
      for (int i = 0; i < _size; ++i) {
        cout << "\t" << i << ": ";
        for (int j = 0; j < _size; ++j) {
          if (adj_mat[i][j] > 0) cout << j << " ";
        }
        cout << endl;
      }
#endif
      // measure network connectivity
      vector<vector<int> > strong_cnctd_comps;
      vector<int> new_component;
      vector<bool> visited(_size, false);
      for (int r = 0; r < _size; ++r) {
        if (_router_states[r] == false) continue;
        if (visited[r] == true) continue;

        deque<int> bfs_q;
        bfs_q.push_back(r);
        visited[r] = true;
        new_component.clear();

        while (!bfs_q.empty()) {
          int n = bfs_q.front();
          bfs_q.pop_front();
          new_component.push_back(n);
          for (int i = 0; i < _size; ++i) {
            if (adj_mat[n][i] == 1 && visited[i] == false) {
              visited[i] = true;
              bfs_q.push_back(i);
            }
          }
        }
        sort(new_component.begin(), new_component.end());
#ifdef DEBUG_POWERGATE_CONFIG
        cout << "component " << strong_cnctd_comps.size() << ": ";
        for (unsigned k = 0; k < new_component.size(); ++k) {
          cout << new_component[k] << ", ";
        }
        cout << endl;
#endif
        strong_cnctd_comps.push_back(new_component);
      }
      if (strong_cnctd_comps.size() == 1) {
#ifdef DEBUG_POWERGATE_CONFIG
        cout << "network is connected" << endl;
#endif
      } else {
#ifdef DEBUG_POWERGATE_CONFIG
        cout << "network is disjoint with " << strong_cnctd_comps.size() << " partitions" << endl;
#endif
        vector<bool> is_edge_router(_size, false);
        for (int rid = 0; rid < _size; ++rid) {
          int num_neighbors = 0;
          for (int j = 0; j < _size; ++j) {
            if (adj_mat[rid][j] == 1)
              ++num_neighbors;
          }
          if ( ((rid == 0 || rid == gK - 1 || rid == _size - 1 || rid == _size - gK) && num_neighbors < 2) ||
              ((rid / gK == 0 || rid % gK == 0 || rid / gK == gK - 1 || rid % gK == gK - 1) && num_neighbors < 3) ||
              ((rid / gK > 0 && rid % gK > 0 && rid / gK < gK - 1 && rid % gK < gK - 1) && num_neighbors < 4) )
            is_edge_router[rid] = true;
        }
        for (unsigned i = 0; i < strong_cnctd_comps.size(); ++i) {
          vector<int> component = strong_cnctd_comps[i];
          if (find(component.begin(), component.end(), _fabric_manager) != component.end())
            continue;

          vector<int> connected_routers;
          unsigned num_off_routers_on_path = _size;
          int fx = _fabric_manager % gK;
          int fy = _fabric_manager / gK;
          for (int k = 0; k < 8; ++k) {
            int j = RandomInt(component.size() - 1);
            int edge_rid = component[j];
            while (is_edge_router[edge_rid] == false) {
              j = RandomInt(component.size() - 1);
              edge_rid = component[j];
            }
            int rx = edge_rid % gK;
            int ry = edge_rid / gK;
            int r = edge_rid;
            vector<int> off_routers_on_path;
            // x dimention
            if (rx > fx) {
              for (int d = 1; d <= rx - fx; ++d) {
                --r;
                if (_router_states[r] == false) off_routers_on_path.push_back(r);
              }
            } else {
              for (int d = 1; d <= fx - rx; ++d) {
                ++r;
                if (_router_states[r] == false) off_routers_on_path.push_back(r);
              }
            }
            assert(r % gK == fx);
            // y dimension
            if (ry > fy) {
              for (int d = 1; d <= ry - fy; ++d) {
                r = r - gK;
                if (_router_states[r] == false) off_routers_on_path.push_back(r);
              }
            } else {
              for (int d = 1; d <= fy - ry; ++d) {
                r = r + gK;
                if (_router_states[r] == false) off_routers_on_path.push_back(r);
              }
            }
            assert(r == _fabric_manager);
            if (off_routers_on_path.size() < num_off_routers_on_path) {
              num_off_routers_on_path = off_routers_on_path.size();
              connected_routers.clear();
              connected_routers = off_routers_on_path;
            }
          }
          for (unsigned i = 0; i < connected_routers.size(); ++i) {
            int rid = connected_routers[i];
            _router_states[rid] = true;
          }
        }
      }
    } else if (_powergate_type == "rpc") {  // conservative RP
      for (unsigned i = 0; i < _off_cores.size(); ++i) {
        int cid = _off_cores[i];
        int cx = cid % gK;
        int cy = cid / gK;
        bool neighbors_parked = false;
        // direct neighbors
        if (cx - 1 >= 0) {
          int nid = cid - 1;
          if (_router_states[nid] == false) neighbors_parked = true;
        }
        if (cx + 1 < gK) {
          int nid = cid + 1;
          if (_router_states[nid] == false) neighbors_parked = true;
        }
        if (cy - 1 >= 0) {
          int nid = cid - gK;
          if (_router_states[nid] == false) neighbors_parked = true;
        }
        if (cy + 1 < gK) {
          int nid = cid + gK;
          if (_router_states[nid] == false) neighbors_parked = true;
        }
        // indirect neighbors (diagonal)
        if (cx - 1 >= 0 && cy - 1 >= 0) {
          int nid = cid - gK - 1;
          if (_router_states[nid] == false) neighbors_parked = true;
        }
        if (cx - 1 >= 0 && cy + 1 < gK) {
          int nid = cid + gK - 1;
          if (_router_states[nid] == false) neighbors_parked = true;
        }
        if (cx + 1 < gK && cy - 1 >= 0) {
          int nid = cid - gK + 1;
          if (_router_states[nid] == false) neighbors_parked = true;
        }
        if (cx + 1 < gK && cy + 1 < gK) {
          int nid = cid + gK + 1;
          if (_router_states[nid] == false) neighbors_parked = true;
        }
        if (neighbors_parked == false) {
          _router_states[cid] = false;  // park the router
        }
      }
    } else if (_powergate_type != "no_pg") {
      ostringstream err;
      err << "Unknown power-gating type: " << _powergate_type << endl;
      Error(err.str());
    }
#ifdef DEBUG_POWERGATE_CONFIG
      cout << "core (router) states:";
      for (int r = 0; r < _size; ++r) {
        if (r % gK == 0) cout << endl << "\t";
        if (_core_states[r] == true) {
          cout << "on (";
        } else {
          cout << "of (";
        }
        if (_router_states[r] == true) {
          cout << "on)\t\t";
        } else {
          cout << "of)\t\t";
        }
      }
      cout << endl;
#endif
  } else {
    // Core parking
    for (unsigned i = 0; i < _off_cores.size(); ++i) {
      int c_id = _off_cores[i];
      _core_states[c_id] = false;
    }
    for (unsigned i = 0; i < _off_routers.size(); ++i) {
      int r_id = _off_routers[i];
      _router_states[r_id] = false;
    }
  }
  /* ==== Power Gate - End ==== */

  /*booksim used arrays of flits as the channels which makes have capacity of
   *one. To simulate channel latency, flitchannel class has been added
   *which are fifos with depth = channel latency and each cycle the channel
   *shifts by one
   *credit channels are the necessary counter part
   */
  _inject.resize(_nodes);
  _inject_cred.resize(_nodes);
  for ( int s = 0; s < _nodes; ++s ) {
    ostringstream name;
    name << Name() << "_fchan_ingress" << s;
    _inject[s] = new FlitChannel(this, name.str(), _classes);
    _inject[s]->SetSource(NULL, s);
    _timed_modules.push_back(_inject[s]);
    name.str("");
    name << Name() << "_cchan_ingress" << s;
    _inject_cred[s] = new CreditChannel(this, name.str());
    _timed_modules.push_back(_inject_cred[s]);
  }
  _eject.resize(_nodes);
  _eject_cred.resize(_nodes);
  for ( int d = 0; d < _nodes; ++d ) {
    ostringstream name;
    name << Name() << "_fchan_egress" << d;
    _eject[d] = new FlitChannel(this, name.str(), _classes);
    _eject[d]->SetSink(NULL, d);
    _timed_modules.push_back(_eject[d]);
    name.str("");
    name << Name() << "_cchan_egress" << d;
    _eject_cred[d] = new CreditChannel(this, name.str());
    _timed_modules.push_back(_eject_cred[d]);
  }
  _chan.resize(_channels);
  _chan_cred.resize(_channels);
  /* ==== Power Gate - Begin ==== */
  _chan_handshake.resize(_channels);
  /* ==== Power Gate - End ==== */
  for ( int c = 0; c < _channels; ++c ) {
    ostringstream name;
    name << Name() << "_fchan_" << c;
    _chan[c] = new FlitChannel(this, name.str(), _classes);
    _timed_modules.push_back(_chan[c]);
    name.str("");
    name << Name() << "_cchan_" << c;
    _chan_cred[c] = new CreditChannel(this, name.str());
    _timed_modules.push_back(_chan_cred[c]);
    /* ==== Power Gate - Begin ==== */
    name.str("");
    name << Name() << "_hchan_" << c;
    _chan_handshake[c] = new HandshakeChannel(this, name.str());
    _timed_modules.push_back(_chan_handshake[c]);
    /* ==== Power Gate - End ==== */
  }
}

void Network::ReadInputs( )
{
  for(deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
      iter != _timed_modules.end();
      ++iter) {
    (*iter)->ReadInputs( );
  }
}

/* ==== Power Gate - Begin ==== */
void Network::PowerStateEvaluate( )
{
  for(deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
      iter != _timed_modules.end();
      ++iter) {
    (*iter)->PowerStateEvaluate( );
  }
}
/* ==== Power Gate - End ==== */

void Network::Evaluate( )
{
  for(deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
      iter != _timed_modules.end();
      ++iter) {
    (*iter)->Evaluate( );
  }
}

void Network::WriteOutputs( )
{
  for(deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
      iter != _timed_modules.end();
      ++iter) {
    (*iter)->WriteOutputs( );
  }
}

void Network::WriteFlit( Flit *f, int source )
{
  assert( ( source >= 0 ) && ( source < _nodes ) );
  _inject[source]->Send(f);
}

Flit *Network::ReadFlit( int dest )
{
  assert( ( dest >= 0 ) && ( dest < _nodes ) );
  return _eject[dest]->Receive();
}

void Network::WriteCredit( Credit *c, int dest )
{
  assert( ( dest >= 0 ) && ( dest < _nodes ) );
  _eject_cred[dest]->Send(c);
}

Credit *Network::ReadCredit( int source )
{
  assert( ( source >= 0 ) && ( source < _nodes ) );
  return _inject_cred[source]->Receive();
}

void Network::InsertRandomFaults( const Configuration &config )
{
  Error( "InsertRandomFaults not implemented for this topology!" );
}

void Network::OutChannelFault( int r, int c, bool fault )
{
  assert( ( r >= 0 ) && ( r < _size ) );
  _routers[r]->OutChannelFault( c, fault );
}

double Network::Capacity( ) const
{
  return 1.0;
}

/* this function can be heavily modified to display any information
 * neceesary of the network, by default, call display on each router
 * and display the channel utilization rate
 */
void Network::Display( ostream & os ) const
{
  for ( int r = 0; r < _size; ++r ) {
    _routers[r]->Display( os );
  }
}

void Network::DumpChannelMap( ostream & os, string const & prefix ) const
{
  os << prefix << "source_router,source_port,dest_router,dest_port" << endl;
  for(int c = 0; c < _nodes; ++c)
    os << prefix
       << "-1," 
       << _inject[c]->GetSourcePort() << ',' 
       << _inject[c]->GetSink()->GetID() << ',' 
       << _inject[c]->GetSinkPort() << endl;
  for(int c = 0; c < _channels; ++c)
    os << prefix
       << _chan[c]->GetSource()->GetID() << ',' 
       << _chan[c]->GetSourcePort() << ',' 
       << _chan[c]->GetSink()->GetID() << ',' 
       << _chan[c]->GetSinkPort() << endl;
  for(int c = 0; c < _nodes; ++c)
    os << prefix
       << _eject[c]->GetSource()->GetID() << ',' 
       << _eject[c]->GetSourcePort() << ',' 
       << "-1," 
       << _eject[c]->GetSinkPort() << endl;
}

void Network::DumpNodeMap( ostream & os, string const & prefix ) const
{
  os << prefix << "source_router,dest_router" << endl;
  for(int s = 0; s < _nodes; ++s)
    os << prefix
       << _eject[s]->GetSource()->GetID() << ','
       << _inject[s]->GetSink()->GetID() << endl;
}
