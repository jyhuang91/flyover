/*
 * rptrafficmanager.hh
 * - A traffic manager for Router Parking
 *
 * Author: Jiayi Huang
 */

#ifndef _RPTRAFFICMANAGER_HPP_
#define _RPTRAFFICMANAGER_HPP_

#include <cassert>

#include "mem/ruby/network/booksim2/trafficmanager.hh"

class RPTrafficManager : public TrafficManager {

private:

  vector<vector<int> > _packet_size;
  vector<vector<int> > _packet_size_rate;
  vector<int> _packet_size_max_val;

  // ============ Internal methods ============
protected:

  virtual void _Inject();
  virtual void _Step( );

  virtual void _GeneratePacket( int source, int size, int cl, uint64_t time );

public:

  RPTrafficManager( const Configuration &config, const vector<BSNetwork *> & net );
  virtual ~RPTrafficManager( );

};

#endif
