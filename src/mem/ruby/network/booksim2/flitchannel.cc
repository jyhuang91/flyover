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

// ----------------------------------------------------------------------
//
//  File Name: flitchannel.cpp
//  Author: James Balfour, Rebecca Schultz
//
// ----------------------------------------------------------------------

#include "mem/ruby/network/booksim2/flitchannel.hh"

#include <iostream>
#include <iomanip>

#include "mem/ruby/network/booksim2/routers/router.hh"
#include "mem/ruby/network/booksim2/globals.hh"

// ----------------------------------------------------------------------
//  $Author: jbalfour $
//  $Date: 2007/06/27 23:10:17 $
//  $Id$
// ----------------------------------------------------------------------
FlitChannel::FlitChannel(Module * parent, string const & name, int classes)
: Channel<Flit>(parent, name), _routerSource(NULL), _routerSourcePort(-1), 
  _routerSink(NULL), _routerSinkPort(-1), _idle(0) {
  _active.resize(classes, 0);
}

/* ==== Power Gate - Begin ==== */
//void FlitChannel::SetSource(Router const * const router, int port) {
//  _routerSource = router;
//  _routerSourcePort = port;
//}
//
//void FlitChannel::SetSink(Router const * const router, int port) {
//  _routerSink = router;
//  _routerSinkPort = port;
//}

void FlitChannel::SetSource(Router * router, int port) {
    _routerSource = router;
    _routerSourcePort = port;
}

void FlitChannel::SetSink(Router * router, int port) {
    _routerSink = router;
    _routerSinkPort = port;
}
/* ==== Power Gate - End ==== */

void FlitChannel::Send(Flit * f) {
  if(f) {
    ++_active[f->cl];
  } else {
    ++_idle;
  }
  Channel<Flit>::Send(f);
}

void FlitChannel::ReadInputs() {
  Flit const * const & f = _input;
  if(f && f->watch) {
    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
        << "Beginning channel traversal for flit " << f->id
        << " with delay " << _delay
        << "." << endl;
  }

  /* ==== Power Gate - Begin ==== */
  if (f && _routerSink) {
    if (f->src == _routerSink->GetID())
      _routerSink->WakeUp();
  }
  /* ==== Power Gate - Begin ==== */
  Channel<Flit>::ReadInputs();
}

void FlitChannel::WriteOutputs() {
  Channel<Flit>::WriteOutputs();
  if(_output && _output->watch) {
    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
        << "Completed channel traversal for flit " << _output->id
        << "." << endl;
  }
}

// gem5 methods
bool FlitChannel::functionalRead(Packet *pkt)
{
    if (_input && _input->functionalRead(pkt))
        return true;

    if (_output && _output->functionalRead(pkt))
        return true;

    if (_wait_queue.empty()) {
        return false;
    } else if (_wait_queue.size() == 1) {
        pair<int, Flit *> const & item = _wait_queue.front();
        return item.second->functionalRead(pkt);
    } else {
        ostringstream err;
        err << "More than 1 flit in the flitchannel!" << endl;
        Error(err.str());
    }

    return false;
}

uint32_t FlitChannel::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;
    if (_input)
        num_functional_writes += _input->functionalWrite(pkt);
    if (_output)
        num_functional_writes += _output->functionalWrite(pkt);

    if (_wait_queue.size() == 1) {
        pair<int, Flit *> const & item = _wait_queue.front();
        num_functional_writes += item.second->functionalWrite(pkt);
    } else if (!_wait_queue.empty()) {
        ostringstream err;
        err << "More than 1 flit in the flitchannel!" << endl;
        Error(err.str());
    }

    return num_functional_writes;
}
