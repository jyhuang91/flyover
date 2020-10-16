#ifndef _DSENT_POWER_MODULE_HPP_
#define _DSENT_POWER_MODULE_HPP_

#include <map>

#include "buffer_monitor.hpp"
#include "config_utils.hpp"
#include "flitchannel.hpp"
#include "module.hpp"
#include "network.hpp"
#include "switch_monitor.hpp"

class DSENT_Power_Module : public Module {
 protected:
  // netrok undersimulation
  Network *net;
  int classes;
  // all channels are this width
  double channel_width;

  // constants
  // dynamic
  double energy_per_buffwrite;
  double energy_per_buffread;
  double energy_traverse_xbar;
  double energy_per_arbitratestage1;
  double energy_per_arbitratestage2;
  double energy_distribute_clk;
  double energy_rr_link_traversal;
  double energy_rs_link_traversal;
  // static
  double input_leak;
  double switch_leak;
  double xbar_leak;
  double xbar_sel_dff_leak;
  double clk_tree_leak;
  double pipeline_reg0_leak;       // per bit
  double pipeline_reg1_leak;       // per bit
  double pipeline_reg2_part_leak;  // per bit
  double rr_link_leak;             // per link
  double rs_link_leak;             // per link
  // freq
  double frequency;

 public:
  DSENT_Power_Module(Network *net, const Configuration &config);
  ~DSENT_Power_Module();

  void run();
};
#endif
