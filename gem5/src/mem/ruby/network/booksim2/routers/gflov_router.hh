/*
 * gflov_router.hh
 * - A class for G-FLOV router microarchitecture
 *
 * Author: Jiayi Huang
 */

#ifndef _GFLOV_ROUTER_HPP_
#define _GFLOV_ROUTER_HPP_

#include "mem/ruby/network/booksim2/routers/iq_router.hh"

/* ==== Power Gate - Begin ==== */
class Handshake;
/* ==== Power Gate - End ==== */

class GFLOVRouter : public IQRouter {

protected:

  /* ==== Power Gate - Begin ==== */
  deque<pair<int, Handshake *> > _proc_handshakes;

  map<int, Handshake *> _out_queue_handshakes;

  vector<vector<int> > _credit_counter;
  vector<bool> _clear_credits;

  vector<queue<Handshake *> > _handshake_buffer;


  void _ReceiveHandshakes( );
  /* ==== Power Gate - End ==== */

  virtual void _InternalStep( );

  virtual void _InputQueuing( );

  virtual void _RouteUpdate( );
  virtual void _VCAllocUpdate( );
  virtual void _SWHoldUpdate( );
  virtual void _SWAllocUpdate( );

  virtual void _OutputQueuing( );

  /* ==== Power Gate - Begin ==== */
  void _SendHandshakes( );

  void _FlovStep( );  // fly-over operations
  void _HandshakeEvaluate();
  void _HandshakeResponse();
  /* ==== Power Gate - End ==== */

public:

  GFLOVRouter( Configuration const & config,
      Module *parent, string const & name, int id,
      int inputs, int outputs );

  virtual ~GFLOVRouter( );

  /* ==== Power Gate - Begin ==== */
  virtual void PowerStateEvaluate( );
  /* ==== Power Gate - End ==== */

  virtual void ReadInputs( );
  virtual void WriteOutputs( );

};

#endif
