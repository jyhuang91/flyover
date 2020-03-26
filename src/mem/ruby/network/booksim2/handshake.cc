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

/*handshake.cpp
 *
 *A class for handshakes
 */

#include "mem/ruby/network/booksim2/booksim.hh"
#include "mem/ruby/network/booksim2/handshake.hh"
#include "mem/ruby/network/booksim2/routers/router.hh"

stack<Handshake *> Handshake::_all;
stack<Handshake *> Handshake::_free;

ostream& operator<<(ostream& os, const Handshake& h)
{
  os << "  Handshake ID: " << h.hid << " (" << &h << ") from router: " << h.id;
  if (h.src_state != -1)
    os << ", source state: " << BSRouter::POWERSTATE[h.src_state];
  if (h.new_state != -1)
    os << ", new state: " << BSRouter::POWERSTATE[h.new_state];
  os << ", drain done signal: " << (h.drain_done ? "True" : "False");
  if (h.logical_neighbor != -1)
    os << ", logical neighbor router: " << h.logical_neighbor;
  os << endl;
  return os;
}

Handshake::Handshake()
{
  Reset();
}

void Handshake::Reset()
{
  new_state = -1;
  src_state = -1;
  drain_done = false;
  wakeup = -1;
  id = -1;
  hid = -1;
  logical_neighbor = -1;
}

Handshake * Handshake::New() {
  Handshake * hs;
  if(_free.empty()) {
    hs = new Handshake();
    _all.push(hs);
  } else {
    hs = _free.top();
    hs->Reset();
    _free.pop();
  }
  return hs;
}

void Handshake::Free() {
  _free.push(this);
}

void Handshake::FreeAll() {
  while(!_all.empty()) {
    delete _all.top();
    _all.pop();
  }
}


int Handshake::OutStanding(){
  return _all.size()-_free.size();
}
