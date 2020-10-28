/*
 * nord_router.hh
 * - A class for NoRD router microarchitecture
 *
 * Author: Jiayi Huang
 */

#ifndef _NORD_ROUTER_HPP_
#define _NORD_ROUTER_HPP_

#include "mem/ruby/network/booksim2/routers/iq_router.hh"

/* ==== Power Gate - Begin ==== */
class Handshake;
/* ==== Power Gate - End ==== */

class NoRDRouter : public IQRouter {

protected:

  /* ==== Power Gate - Begin ==== */
  int _routing_deadlock_timeout_threshold;

  deque<pair<int, Handshake *> > _proc_handshakes;

  map<int, Handshake *> _out_queue_handshakes;

  vector<queue<Handshake *> > _handshake_buffer;

  vector<vector<int> > _credit_counter;
  int _pending_credits;
  vector<int> _outstanding_bypass_packets;

  int _nord_wakeup_threshold;
  int _wakeup_monitor_epoch;
  int _wakeup_monitor_vc_requests;

  bool _ReceiveFlits();
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
  void _SendFlits();
  void _SendHandshakes( );

  void _NoRDStep( );  // fly-over operations
  void _HandshakeEvaluate();
  void _HandshakeResponse();
  /* ==== Power Gate - End ==== */

public:

  NoRDRouter( Configuration const & config,
          Module *parent, string const & name, int id,
          int inputs, int outputs );

  virtual ~NoRDRouter( );

  /* ==== Power Gate - Begin ==== */
  virtual void PowerStateEvaluate( );
  virtual void SetRingOutputVCBufferSize(int vc_buf_size);
  virtual int NextPowerEventCycle();
  /* ==== Power Gate - End ==== */

  virtual void ReadInputs( );
  virtual void WriteOutputs( );

};

#endif
