// $Id: network.cpp 5188 2012-08-30 00:31:31Z dub $

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

/*network.cpp
 *
 *This class is the basis of the entire network, it contains, all the routers
 *channels in the network, and is extended by all the network topologies
 *
 */

#include <cassert>
#include <sstream>

#include "booksim.hpp"
#include "network.hpp"

#include "kncube.hpp"
#include "fly.hpp"
#include "cmesh.hpp"
#include "flatfly_onchip.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
#include "anynet.hpp"
#include "dragonfly.hpp"


Network::Network( const Configuration &config, const string & name ) :
  TimedModule( 0, name )
{
  _size     = -1; 
  _nodes    = -1; 
  _channels = -1;
  _classes  = config.GetInt("classes");

  // Jiayi, for router parking
  _rp = config.GetStr("router_parking");
  _off_percentile = config.GetInt("off_percentile");
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

  // Jiayi, Router Parking
  _core_states.resize(_size, true);
  _router_states.resize(_size, true);	// Jiayi
  for ( int r = 0; r < _size; ++r) {
	  switch (_off_percentile) {
		case 10:
			if (r == 1 || r == 3 || r == 9 || r == 12 || r == 14 || r == 46) {
				_core_states[r] = false;
				if (_rp == "aggressive")
					_router_states[r] = false;
				else {
					assert(_rp == "conservative");
					if (r == 1 || r == 12 || r == 14 || r == 46)
						_router_states[r] = false;
				}
			}
			break;
		case 20:
			if (r == 2 || r == 4 || r == 9 || r == 13 || r == 18 || r == 19
					|| r == 23 || r == 26 || r == 29 || r == 36 || r == 42
					|| r == 43 || r == 53) {
				_core_states[r] = false;
				if (_rp == "aggressive")
					_router_states[r] = false;
				else {
					assert(_rp == "conservative");
					if (r == 2 || r == 13 || r == 19 || r == 23 || r == 29
							|| r == 36 || r == 42 || r == 53)
						_router_states[r] = false;
				}
			}
			break;
		case 30:
			if (r == 1 || r == 6 || r == 8 || r == 9 || r == 11 || r == 13
					|| r == 15 || r == 18 || r == 22 || r == 28 || r == 29
					|| r == 33 || r == 37 || r == 45 || r == 46 || r == 47
					|| r == 51 || r == 55) {
				_core_states[r] = false;
				if (_rp == "aggressive") {
					if (r != 1 && r != 6 && r != 22)
						_router_states[r] = false;
				} else {
					assert(_rp == "conservative");
					if (r == 9 || r == 11 || r == 13 || r == 15 || r == 29
							|| r == 33 || r == 45 || r == 47 || r == 51)
						_router_states[r] = false;
				}
			}
			break;
		case 40:
			if (r == 0 || r == 2 || r == 3 || r == 5 || r == 9 || r == 10
					|| r == 11 || r == 13 || r == 15 || r == 16 || r == 21
					|| r == 22 || r == 23 || r == 31 || r == 32 || r == 33
					|| r == 35 || r == 38 || r == 39 || r == 40 || r == 41
					|| r == 45 || r == 49 || r == 53 || r == 54 || r == 55) {
				_core_states[r] = false;
				if (_rp == "aggressive") {
					if (r != 9 && r != 13 && r != 38)
						_router_states[r] = false;
				} else {
					assert(_rp == "conservative");
					if (r == 0 || r == 3 || r == 5 || r == 15 || r == 16
							|| r == 21 || r == 31 || r == 33 || r == 35
							|| r == 45 || r == 49 || r == 55)
						_router_states[r] = false;
				}
			}
			break;
	  case 50:
			if (r == 1 || r == 2 || r == 3 || r == 4 || r == 6 || r == 7
					|| r == 13 || r == 16 || r == 17 || r == 19 || r == 20
					|| r == 21 || r == 22 || r == 23 || r == 24 || r == 31
					|| r == 32 || r == 37 || r == 38 || r == 41 || r == 42
					|| r == 43 || r == 44 || r == 45 || r == 46 || r == 47
					|| r == 48 || r == 49 || r == 52 || r == 53 || r == 54
					|| r == 55) {
				_core_states[r] = false;
				if (_rp == "aggressive") {
					if (r != 6 && r != 23 && r != 31 && r != 41 && r != 43)
						_router_states[r] = false;
				} else {
					assert(_rp == "conservative");
					if (r == 2 || r == 4 || r == 7 || r == 17 || r == 20
							|| r == 22 || r == 32 || r == 37 || r == 42
							|| r == 47 || r == 48 || r == 53)
						_router_states[r] = false;
				}
			}
			break;
	  case 60:
			if (r != 0 && r != 2 && r != 8 && r != 12 && r != 15 && r != 27
					&& r != 29 && r != 30 && r != 31 && r != 32 && r != 33
					&& r != 35 && r != 40 && r != 46 && r != 47 && r != 48
					&& r != 50 && r != 51 && !(r >= 56)) {
				_core_states[r] = false;
				if (_rp == "aggressive")
					if (r != 1 && r != 10 && r != 18 && r != 20 && r != 23
							&& r != 26 && r != 28 && r != 39 && r != 43)
						_router_states[r] = false;
			}
			if (_rp != "aggressive") {
				assert(_rp == "conservative");
				if (r == 3 || r == 5 || r == 7 || r == 9 || r == 19 || r == 22
						|| r == 25 || r == 34 || r == 36 || r == 38 || r == 49
						|| r == 53 || r == 55) {
					assert(_router_states[r]);
					_router_states[r] = false;
				}
			}
			break;
	  case 70:
			if (r != 15 && r != 16 && r != 17 && r != 18 && r != 24 && r != 27
					&& r != 30 && r != 32 && r != 42 && r != 46 && r != 52
					&& !(r >= 56)) {
				_core_states[r] = false;
				if (_rp == "aggressive")
					if (r != 19 && r != 23 && r != 28 && r != 29 && r != 31
							&& r != 36 && r != 38 && r != 43 && r != 44)
						_router_states[r] = false;
			}
			if (_rp != "aggressive") {
				assert(_rp == "conservative");
				if (r == 1 || r == 3 || r == 5 || r == 7 || r == 20 || r == 22
						|| r == 25 || r == 35 || r == 37 || r == 39 || r == 41
						|| r == 51 || r == 53 || r == 55) {
					assert(_router_states[r]);
					_router_states[r] = false;
				}
			}
			break;
	  case 80:
		  if (r != 0 && r != 9 && r != 17 && r != 27 && r != 35 && !(r >= 56)) {
			  _core_states[r] = false;
			  if (_rp == "aggressive")
				  if (r != 1 && r != 25 && r != 26 && r != 43 && r != 51)
						_router_states[r] = false;
			}
			if (_rp != "aggressive") {
				assert(_rp == "conservative");
				assert(_router_states[r]);
				if (r == 3 || r == 5 || r == 7 || r == 8 || r == 19 || r == 21
						|| r == 23 || r == 24 || r == 34 || r == 37 || r == 39
						|| r == 40 || r == 50 || r == 53 || r == 55)
					_router_states[r] = false;
			}
			break;
		case 29: // for throughput, 29 cores off
			if (r == 1 || r == 2 || r == 3 || r == 4 || r == 6 || r == 7
					|| r == 16 || r == 17 || r == 19 || r == 21 || r == 22
					|| r == 23 || r == 24 || r == 31 || r == 32 || r == 37
					|| r == 38 || r == 39 || (r >= 43 && r <= 49)
					|| (r >= 52 && r <= 55)) {
				_core_states[r] = false;
				if (_rp == "aggressive")
					_router_states[r] = false;
				else {
					assert(_rp == "conservative");
					if (r == 1 || r == 3 || r == 6 || r == 16 || r == 19
							|| r == 21 || r == 23 || r == 32 || r == 37
							|| r == 39 || r == 43 || r == 48 || r == 53
							|| r == 55)
						_router_states[r] = false;
				}
			}
//			if (r == 1 || r == 2 || r == 5 || r == 6 || r == 8 || r == 9 || r == 10
//					|| r == 11 || r == 12 || r == 13 || r == 14 || r == 22 || r == 25
//					|| r == 26 || r == 29 || r == 31 || r == 32 || r == 33 || r == 34
//					|| r == 36 || r == 37 || r == 38 || r == 39 || r == 40 || r == 42
//					|| r == 44 || r == 45 || r == 47 || r == 55) {
//				_core_states[r] = false;
//				if (_rp == "aggressive") {
//					if (r != 8 && r != 11 && r != 22)
//						_router_states[r] = false;
//				} else if (_rp != "on") {
//					assert(_rp == "conservative");
//					if (r == 1 || r == 5 || r == 11 || r == 22 || r == 25
//							|| r == 36 || r == 38 || r == 32 || r == 40
//							|| r == 42 || r == 55)
//						_router_states[r] = false;
//				}
//			if (r == 2 || r == 4 || r == 6 || r == 8 || r == 9 || r == 13 || r == 15
//					|| r == 17 || r == 20 || r == 22 || r == 24 || r == 25 || r == 28
//					|| r == 33 || r == 34 || r == 35 || r == 37 || r == 38 || r == 39
//					|| r == 42 || r == 43 || r == 44 || r == 45 || r == 46 || r == 47
//					|| r == 49 || r == 52 || r == 54 || r == 55) {
//				_core_states[r] = false;
//				if (_rp == "aggressive") {
//					if (r != 2 && r != 6 && r != 13 && r != 17 && r != 35 && r != 43)
//						_router_states[r] = false;
//				} else {
//					assert(_rp == "conservative");
//					if (r == 2 || r == 4 || r == 6 || r == 8 || r == 20
//							|| r == 24 || r == 34 || r == 37 || r == 39
//							|| r == 49 || r == 52 || r == 54)
//						_router_states[r] = false;
//				}
//			}
			break;
		default:
			break;
  	//if (r == 1 || r == 3 || r == 9 || r == 12 || r == 14 || r == 46)
  	//if (r == 2 || r == 4 || r == 9 || r == 13 || r == 18 || r == 19 || r == 23 || r == 26 || r == 29 || r == 36 || r == 42 || r == 43 || r == 53)
  	//if (r == 1 || r == 6 || r == 8 || r == 9 || r == 11 || r == 13 || r == 15 || r == 18 || r == 22 || r == 28 || r == 29 || r == 33 || r == 37
  //			|| r == 45 || r == 46 || r == 47 || r == 51 || r == 55)
  	//if (r == 0 || r == 2 || r == 3 || r == 5 || r == 9 || r == 10 || r == 11 || r ==13 || r == 15 || r == 16 || r == 21 || r == 22 || r == 23 || r == 31 || r == 32 || r == 33
  	//		|| r == 35 || r == 38 || r == 39 || r == 40 || r == 41 || r == 45 || r == 49 || r == 53 || r == 54 || r == 55)
  	//if (r == 1 || r == 2 || r == 3 || r == 4 || r == 6 || r == 7 || r == 13 || r == 16 || r == 17 || r == 19 || r == 20 || r == 21
  	//		|| r == 22 || r == 23 || r == 24 || r == 31 || r == 32 || r == 41 || r == 42 || r == 43 || r == 44 || r == 45 || r == 46
  	//		|| r == 47 || r == 48 || r == 49 || r == 52 || r == 53 || r == 54 || r == 55)
  	//if (r != 0 && r != 2 && r != 8 && r != 12 && r != 15 && r != 27 && r != 29 && r != 30 && r != 31 && r != 32 && r != 33
  	//		&& r != 35 && r != 40 && r != 46 && r != 47 && r != 48 && r != 50 && r != 51 && !(r >= 56))
  	//if (r != 15 && r != 16 && r != 17 && r != 18 && r != 24 && r != 27 && r != 30 && r != 32 && r != 42 && r != 46 && r != 52 && !(r >= 56))
  	//if (r != 0 && r != 9 && r != 17 && r != 24 && r != 27 && r != 35 && !(r >= 56))
//    if (r == 1 || r == 2 || r == 3 || r == 4 || r == 6 || r == 7 || r == 16 || r == 17 || r == 19 || r == 21 || r == 21 || r == 22 || r == 23
//        || r == 24 || r == 31 || r == 32 || r == 37 || r == 38 || r == 39 || r == 43 || r == 44 || r == 45 || r == 46 || r == 47 || r == 48
//        || r == 49 || r == 52 || r == 53 || r == 54 || r == 55) // 29 cores
	  }
  }

  // Jiayi, initialize adjacent matrix
  _fabric_manager = 27;
  _adj_matrix.resize(_size);
  for (int i = 0; i < _size; ++i)
	  _adj_matrix[i].resize(_size, -1);
  // build adjacent matrix
  for (int r = 0; r < _size; ++r) {
	  int r_x = r % 8;
	  int r_y = r / 8;

	  for (int j = 0; j < _size; ++j) {
		  if (_router_states[r] == false) // source turned off
			  continue;
		  if (_router_states[j] == false) // destination turned off
			  continue;

		  int j_x = j % 8;
		  int j_y = j / 8;

		  if (r_x == j_x && r_y == j_y)
			  _adj_matrix[r][j] = 0; // Same node! Check this again
		  else if (((abs(r_x - j_x) == 1) && r_y == j_y)
				  || ((abs(r_y - j_y) == 1) && r_x == j_x))
			  _adj_matrix[r][j] = 1; // connected
	  }
  }
//  cout << "printing the adjacent matrix:" << endl;
//  for (int r = 0; r < _size; ++r) {
//	  for (int j = 0; j < _size; ++j)
//		  cout << _adj_matrix[r][j] << "\t";
//	  cout << endl;
//  }

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
  for ( int c = 0; c < _channels; ++c ) {
    ostringstream name;
    name << Name() << "_fchan_" << c;
    _chan[c] = new FlitChannel(this, name.str(), _classes);
    _timed_modules.push_back(_chan[c]);
    name.str("");
    name << Name() << "_cchan_" << c;
    _chan_cred[c] = new CreditChannel(this, name.str());
    _timed_modules.push_back(_chan_cred[c]);
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
