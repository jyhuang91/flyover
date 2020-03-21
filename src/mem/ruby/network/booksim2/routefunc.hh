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

#ifndef _ROUTEFUNC_HPP_
#define _ROUTEFUNC_HPP_

#include "mem/ruby/network/booksim2/flit.hh"
#include "mem/ruby/network/booksim2/routers/router.hh"
#include "mem/ruby/network/booksim2/outputset.hh"
#include "mem/ruby/network/booksim2/config_utils.hh"

typedef void (*tRoutingFunction)( const Router *, const Flit *, int in_channel, OutputSet *, bool );

void InitializeRoutingMap( const Configuration & config );

/* ==== Power Gate - Begin ==== */
enum Direction {
    DIR_EAST = 0,
    DIR_WEST,
    DIR_SOUTH,
    DIR_NORTH,
    DIR_NI,
    DIR_INVALID,
};

extern int gRoutingDeadlockTimeoutThreshold;
extern int gMissRouteThreshold;
/* ==== Power Gate - End ==== */

extern map<string, tRoutingFunction> gRoutingFunctionMap;

extern int gNumVCs;
extern int gReadReqBeginVC, gReadReqEndVC;
extern int gWriteReqBeginVC, gWriteReqEndVC;
extern int gReadReplyBeginVC, gReadReplyEndVC;
extern int gWriteReplyBeginVC, gWriteReplyEndVC;

#endif
