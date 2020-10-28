/*
 * rp_router.hh
 * - A class for Router Parking router
 *
 * Author: Jiayi Huang
 */

#ifndef _RP_ROUTER_HPP_
#define _RP_ROUTER_HPP_

#include "mem/ruby/network/booksim2/routers/iq_router.hh"

class RPRouter : public IQRouter {

protected:

  virtual void _InternalStep( );

  virtual void _InputQueuing( );

  virtual void _RouteUpdate( );
  virtual void _VCAllocUpdate( );
  virtual void _SWHoldUpdate( );
  virtual void _SWAllocUpdate( );

public:

  RPRouter( Configuration const & config,
          Module *parent, string const & name, int id,
          int inputs, int outputs );

  virtual ~RPRouter( );

  virtual void PowerStateEvaluate( );

};

#endif
