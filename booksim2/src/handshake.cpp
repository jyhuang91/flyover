/*
 * handshake.cpp
 * - handshake signal class
 *
 * Author: Jiayi Huang
*/

#include "booksim.hpp"
#include "handshake.hpp"
#include "routers/router.hpp"

stack<Handshake *> Handshake::_all;
stack<Handshake *> Handshake::_free;

ostream& operator<<(ostream& os, const Handshake& h)
{
  os << "  Handshake ID: " << h.hid << " (" << &h << ") from router: " << h.id;
  if (h.src_state != -1)
    os << ", source state: " << Router::POWERSTATE[h.src_state];
  if (h.new_state != -1)
    os << ", new state: " << Router::POWERSTATE[h.new_state];
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
