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
//  BSArbiter: Base class for Matrix and Round Robin BSArbiter
//
// ----------------------------------------------------------------------

#include "mem/ruby/network/booksim2/arbiters/arbiter.hh"
#include "mem/ruby/network/booksim2/arbiters/roundrobin_arb.hh"
#include "mem/ruby/network/booksim2/arbiters/matrix_arb.hh"
#include "mem/ruby/network/booksim2/arbiters/tree_arb.hh"

#include <limits>
#include <cassert>

using namespace std ;

BSArbiter::BSArbiter( Module *parent, const string &name, int size )
  : Module( parent, name ),
    _size(size), _selected(-1), _highest_pri(numeric_limits<int>::min()),
    _best_input(-1), _num_reqs(0)
{
  _request.resize(size);
  for ( int i = 0 ; i < size ; i++ )
    _request[i].valid = false ;
}

void BSArbiter::AddRequest( int input, int id, int pri )
{
  assert( 0 <= input && input < _size ) ;
  assert( !_request[input].valid );

  _num_reqs++ ;
  _request[input].valid = true ;
  _request[input].id = id ;
  _request[input].pri = pri ;
}

int BSArbiter::Arbitrate( int* id, int* pri )
{
  if ( _selected != -1 ) {
    if ( id )
      *id  = _request[_selected].id ;
    if ( pri )
      *pri = _request[_selected].pri ;
  }

  assert((_selected >= 0) || (_num_reqs == 0));

  return _selected ;
}

void BSArbiter::Clear()
{
  if(_num_reqs > 0) {

    // clear the request vector
    for ( int i = 0; i < _size ; i++ )
      _request[i].valid = false ;
    _num_reqs = 0 ;
    _selected = -1;
  }
}

BSArbiter *BSArbiter::NewArbiter( Module *parent, const string& name,
			      const string &arb_type, int size)
{
  BSArbiter *a = NULL;
  if(arb_type == "round_robin") {
    a = new RoundRobinArbiter( parent, name, size );
  } else if(arb_type == "matrix") {
    a = new BSMatrixArbiter( parent, name, size );
  } else if(arb_type.substr(0, 5) == "tree(") {
    size_t left = 4;
    size_t middle = arb_type.find_first_of(',');
    assert(middle != string::npos);
    size_t right = arb_type.find_last_of(')');
    assert(right != string::npos);
    string groups_str = arb_type.substr(left+1, middle-left-1);
    int groups = atoi(groups_str.c_str());
    string sub_arb_type = arb_type.substr(middle+1, right-middle-1);
    a = new TreeArbiter( parent, name, size, groups, sub_arb_type );
  } else assert(false);
  return a;
}
