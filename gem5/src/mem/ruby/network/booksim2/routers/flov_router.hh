/*
 * flov_router.hh
 * - A class for FLOV router microarchitecture
 *
 * Author: Jiayi Huang
 */

#ifndef _FLOV_ROUTER_HPP_
#define _FLOV_ROUTER_HPP_

#include "mem/ruby/network/booksim2/routers/iq_router.hh"

/* ==== Power Gate - Begin ==== */
class Handshake;
/* ==== Power Gate - End ==== */

class FLOVRouter : public IQRouter {

/* ==== Power Gate - Begin ==== */
public:
  enum eFLOVPolicy { state_min = 0, gflov = state_min, rflov, noflov,
    state_max = noflov };
  static const char * const FLOVPOLICY[];
/* ==== Power Gate - End ==== */

protected:

  /* ==== Power Gate - Begin ==== */
  deque<pair<int, Handshake *> > _proc_handshakes;

  map<int, Handshake *> _out_queue_handshakes;

  vector<vector<int> > _credit_counter;
  vector<bool> _clear_credits;

  vector<queue<Handshake *> > _handshake_buffer;

  eFLOVPolicy _flov_policy;

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
  void _RFLOVPowerStateEvaluate();
  void _GFLOVPowerStateEvaluate();
  void _NoFLOVPowerStateEvaluate();
  /* ==== Power Gate - End ==== */

public:

  FLOVRouter( Configuration const & config,
      Module *parent, string const & name, int id,
      int inputs, int outputs );

  virtual ~FLOVRouter( );

  /* ==== Power Gate - Begin ==== */
  virtual void PowerStateEvaluate( );
  virtual void AggressFLOVPolicy();
  virtual void RegressFLOVPolicy();
  virtual inline void AggressPowerGatingPolicy() { AggressFLOVPolicy(); }
  virtual inline void RegressPowerGatingPolicy() { RegressFLOVPolicy(); }
  virtual int NextPowerEventCycle();
  /* ==== Power Gate - End ==== */

  virtual void ReadInputs( );
  virtual void WriteOutputs( );

};

#endif
