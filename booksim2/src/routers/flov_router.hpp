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

#ifndef _FLOV_ROUTER_HPP_
#define _FLOV_ROUTER_HPP_

#include "iq_router.hpp"

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
  /* ==== Power Gate - End ==== */

  virtual void ReadInputs( );
  virtual void WriteOutputs( );

};

#endif