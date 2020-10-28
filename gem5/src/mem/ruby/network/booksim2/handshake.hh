/*
 * handshake.hh
 * - handshake signal class
 *
 * Author: Jiayi Huang
*/

#ifndef _HANDSHAKE_HPP_
#define _HANDSHAKE_HPP_

#include <iostream>
#include <set>
#include <stack>

class Handshake {

public:

  int new_state;
  int src_state;
  bool drain_done;
  int wakeup;
  int id;
  int hid;
  int logical_neighbor;

  void Reset();

  static Handshake * New();
  void Free();
  static void FreeAll();
  static int OutStanding();
private:

  static stack<Handshake *> _all;
  static stack<Handshake *> _free;

  Handshake();
  ~Handshake() {}

};

ostream& operator<<(ostream& os, const Handshake& h);

#endif
