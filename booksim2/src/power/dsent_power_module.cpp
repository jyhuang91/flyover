/*
 * dsent_power_module.cpp
 * - Parameterized power model for DSENT numbers
 *
 * Author: Jiayi Huang
 */

#include "dsent_power_module.hpp"
#include "booksim_config.hpp"
#include "iq_router.hpp"

DSENT_Power_Module::DSENT_Power_Module(Network *n, const Configuration &config)
    : Module(0, "dsent_power_module") {
  net = n;
  classes = config.GetInt("classes");
  channel_width = (double)config.GetInt("channel_width");

  energy_per_buffwrite = config.GetFloat("energy_per_buffwrite");
  energy_per_buffread = config.GetFloat("energy_per_buffread");
  energy_traverse_xbar = config.GetFloat("energy_traverse_xbar");
  energy_per_arbitratestage1 = config.GetFloat("energy_per_arbitratestage1");
  energy_per_arbitratestage2 = config.GetFloat("energy_per_arbitratestage2");
  energy_distribute_clk = config.GetFloat("energy_distribute_clk");
  energy_rr_link_traversal = config.GetFloat("energy_rr_link_traversal");
  energy_rs_link_traversal = config.GetFloat("energy_rs_link_traversal");

  input_leak = config.GetFloat("input_leak");
  switch_leak = config.GetFloat("switch_leak");
  xbar_leak = config.GetFloat("xbar_leak");
  xbar_sel_dff_leak = config.GetFloat("xbar_sel_dff_leak");
  clk_tree_leak = config.GetFloat("clk_tree_leak");
  pipeline_reg0_leak = config.GetFloat("pipeline_reg0_leak");
  pipeline_reg1_leak = config.GetFloat("pipeline_reg1_leak");
  pipeline_reg2_part_leak = config.GetFloat("pipeline_reg2_part_leak");
  rr_link_leak = config.GetFloat("rr_link_leak");
  rs_link_leak = config.GetFloat("rs_link_leak");

  frequency = config.GetFloat("frequency");
}

DSENT_Power_Module::~DSENT_Power_Module() {}

void DSENT_Power_Module::run() {
  // link power
  double link_dynamic_energy = 0;
  double link_dynamic_power = 0;
  double link_leakage_power = 0;
  // buffer power
  double buf_read_energy = 0;
  double buf_write_energy = 0;
  double buf_dynamic_power = 0;
  double buf_read_power = 0;
  double buf_write_power = 0;
  double buf_leakage_power = 0;
  // Switch allocator power
  double sw_dynamic_energy = 0;
  double sw_dynamic_power = 0;
  double sw_leakage_power = 0;
  // crossbar power
  double xbar_dynamic_energy = 0;
  double xbar_dynamic_power = 0;
  double xbar_leakage_power = 0;
  // clock power
  double clk_dynamic_power = 0;
  double clk_leakage_power = 0;
  // power gate overhead
  double power_gate_energy = 0;
  double power_gate_power = 0;

  vector<FlitChannel *> inject = net->GetInject();
  vector<FlitChannel *> eject = net->GetEject();
  vector<FlitChannel *> chan = net->GetChannels();

  double totalTime = GetSimTime();

  // accumulate network energy
  netEnergyStats &net_energy_stats = net->GetNetEnergyStats();

  // inject and eject link power
  for (int i = 0; i < net->NumNodes(); i++) {
    // inject and eject channel: site-to-router link
    const FlitChannel *f_inject = inject[i];
    const FlitChannel *f_eject = eject[i];
    // activity factor for inject
    const vector<int> temp_inject = f_inject->GetActivity();
    const vector<int> temp_eject = f_eject->GetActivity();
    vector<double> a_inject(classes);
    vector<double> a_eject(classes);
    for (int j = 0; j < classes; ++j) {
      a_inject[j] = ((double)temp_inject[j]);
      a_eject[j] = ((double)temp_eject[j]);
      link_dynamic_energy +=
          (a_inject[j] + a_eject[j]) * energy_rs_link_traversal;
    }
    link_leakage_power += rs_link_leak * 2;  // one inject and one eject
  }

  // network link power
  for (int i = 0; i < net->NumChannels(); ++i) {
    const FlitChannel *f = chan[i];
    const vector<int> temp = f->GetActivity();
    vector<double> a(classes);
    for (int j = 0; j < classes; ++j) {
      a[j] = ((double)temp[j]);
      link_dynamic_energy += a[j] * energy_rr_link_traversal;
    }
    link_leakage_power += rr_link_leak;
  }

  // router power
  vector<Router *> routers = net->GetRouters();
  for (size_t r = 0; r < routers.size(); ++r) {
    IQRouter *temp = dynamic_cast<IQRouter *>(routers[r]);

    double power_off_cycles = temp->GetPowerOffCycles();
    assert(power_off_cycles <= totalTime);
    temp->ResetPowerOffCycles();
    double pg_overhead_cycles = temp->GetPowerGateOverheadCycles();
    // assert(pg_overhead_cycles >= 0 && pg_overhead_cycles <=
    // power_off_cycles);

    // buffer
    const BufferMonitor *bm = temp->GetBufferMonitor();
    const vector<int> reads = bm->GetReads();
    const vector<int> writes = bm->GetWrites();
    for (int i = 0; i < bm->NumInputs(); ++i) {
      buf_leakage_power +=
          (input_leak +
           (pipeline_reg0_leak + pipeline_reg1_leak) * channel_width) *
          (totalTime - power_off_cycles) / totalTime;
      power_gate_energy +=
          (input_leak +
           (pipeline_reg0_leak + pipeline_reg1_leak) * channel_width) *
          pg_overhead_cycles / frequency;
      for (int j = 0; j < classes; ++j) {
        double ar = ((double)reads[i * classes + j]);
        double aw = ((double)writes[i * classes + j]);
        buf_read_energy += ar * energy_per_buffread;
        buf_write_energy += aw * energy_per_buffwrite;
      }
    }

    // switch allocator and crossbar
    const SwitchMonitor *sm = temp->GetSwitchMonitor();
    sw_leakage_power +=
        switch_leak * (totalTime - power_off_cycles) / totalTime;
    xbar_leakage_power += (xbar_leak + xbar_sel_dff_leak) *
                          (totalTime - power_off_cycles) / totalTime;
    power_gate_power += (switch_leak + xbar_leak + xbar_sel_dff_leak) *
                        pg_overhead_cycles / frequency;
    const vector<int> activity = sm->GetActivity();
    vector<double> type_activity(classes);
    for (int i = 0; i < sm->NumOutputs(); ++i) {
      xbar_leakage_power += (pipeline_reg2_part_leak * channel_width) *
                            (totalTime - power_off_cycles) / totalTime;
      power_gate_power += (pipeline_reg2_part_leak * channel_width) *
                          pg_overhead_cycles / frequency;
      for (int j = 0; j < sm->NumInputs(); ++j) {
        for (int k = 0; k < classes; ++k) {
          double a = activity[k + classes * (i + sm->NumOutputs() * j)];
          sw_dynamic_energy +=
              a * (energy_per_arbitratestage1 + energy_per_arbitratestage2);
          xbar_dynamic_energy += a * energy_traverse_xbar;
        }
      }
    }

    // clock power
    clk_dynamic_power += energy_distribute_clk * frequency;
    clk_leakage_power += clk_tree_leak;
  }

  net_energy_stats.tot_time += totalTime;
  double total_run_time = net_energy_stats.tot_time / frequency;
  double kernel_run_time = totalTime / frequency;

  // accumulate network energy

  // link
  double temp = net_energy_stats.link_dynamic_energy;
  net_energy_stats.link_dynamic_energy = link_dynamic_energy;
  net_energy_stats.link_leakage_energy += link_leakage_power * kernel_run_time;
  link_dynamic_power = (link_dynamic_energy - temp) / kernel_run_time;

  // buffer
  temp = net_energy_stats.buf_rd_energy;
  net_energy_stats.buf_rd_energy = buf_read_energy;
  buf_read_power = (buf_read_energy - temp) / kernel_run_time;

  temp = net_energy_stats.buf_wt_energy;
  net_energy_stats.buf_wt_energy = buf_write_energy;
  buf_write_power = (buf_write_energy - temp) / kernel_run_time;

  net_energy_stats.buf_leakage_energy += buf_leakage_power * kernel_run_time;

  // switch
  temp = net_energy_stats.sw_dynamic_energy;
  net_energy_stats.sw_dynamic_energy = sw_dynamic_energy;
  net_energy_stats.sw_leakage_energy += sw_leakage_power * kernel_run_time;
  sw_dynamic_power = (sw_dynamic_energy - temp) / kernel_run_time;

  // xbar
  temp = net_energy_stats.xbar_dynamic_energy;
  net_energy_stats.xbar_dynamic_energy = xbar_dynamic_energy;
  net_energy_stats.xbar_leakage_energy += xbar_leakage_power * kernel_run_time;
  xbar_dynamic_power = (xbar_dynamic_energy - temp) / kernel_run_time;

  // clk
  net_energy_stats.clk_dynamic_energy += clk_dynamic_power * kernel_run_time;
  net_energy_stats.clk_leakage_energy += clk_leakage_power * kernel_run_time;

  // power gating overhead
  temp = net_energy_stats.power_gate_energy;
  net_energy_stats.power_gate_energy = power_gate_energy;
  power_gate_power = (power_gate_energy - temp) / kernel_run_time;

  // router and network energy
  net_energy_stats.ComputeTotalEnergy();

  buf_dynamic_power = buf_read_power + buf_write_power;
  double total_dynamic_power = buf_dynamic_power + sw_dynamic_power +
                               xbar_dynamic_power + clk_dynamic_power +
                               link_dynamic_power;
  double total_leakage_power = buf_leakage_power + sw_leakage_power +
                               xbar_leakage_power + clk_leakage_power +
                               link_leakage_power;
  double total_power =
      total_dynamic_power + total_leakage_power + power_gate_power;

  cout << "-----------------------------------------\n";
  cout << "DSENT Power Model\n";

  cout << "- Kernel OCN Power Summary\n";
  cout << "- Completion Time (cycle):  " << totalTime << "\n";
  cout << "- Flit Widths:              " << channel_width << "\n";
  cout << "---\n";

  cout << "- Link Dynamic Power:       " << link_dynamic_power << "\n";
  cout << "- Link Leakage Power:       " << link_leakage_power << "\n";
  cout << "---\n";

  cout << "- Buffer Read Power:        " << buf_read_power << "\n";
  cout << "- Buffer Write Power:       " << buf_write_power << "\n";
  cout << "- Buffer Dynamic Power:     " << buf_dynamic_power << "\n";
  cout << "- Buffer Leakage Power:     " << buf_leakage_power << "\n";
  cout << "---\n";

  cout << "- Switch Dynamic Power:     " << sw_dynamic_power << "\n";
  cout << "- Switch Leakage Power:     " << sw_leakage_power << "\n";
  cout << "---\n";

  cout << "- Crossbar Dynamic Power:   " << xbar_dynamic_power << "\n";
  cout << "- Crossbar Leakage Power:   " << xbar_leakage_power << "\n";
  cout << "---\n";

  cout << "- Clk Dynamic Power:        " << clk_dynamic_power << "\n";
  cout << "- Clk Static Power:         " << clk_leakage_power << "\n";
  cout << "---\n";

  cout << "- Power Gating Overhead:    " << power_gate_power << "\n";
  cout << "---\n";

  cout << "- NoC Power (Including links):\n";
  cout << "- Total Dynamic Power:      " << total_dynamic_power << "\n";
  cout << "- Total Leakage Power:      " << total_leakage_power << "\n";
  cout << "- Total PowerGate Overhead: " << power_gate_power << "\n";
  cout << "- Total Power:              " << total_power << "\n";
  cout << "- PG Overhead Portion:      " << power_gate_power * 100 / total_power
       << "\n";
  cout << "- Leakage Portion:          "
       << total_leakage_power * 100 / total_power << "%\n";
  cout << "---\n";

  cout << "- Routers Power (Excluding links):\n";
  cout << "- Total Dynamic Power:      "
       << total_dynamic_power - link_dynamic_power << "\n";
  cout << "- Total Leakage Power:      "
       << total_leakage_power - link_leakage_power << "\n";
  cout << "- Total PowerGate Overhead: " << power_gate_power << "\n";
  cout << "- Total Power:              "
       << total_power - link_dynamic_power - link_leakage_power << "\n";
  cout << "- PG Overhead Portion:      "
       << power_gate_power * 100 /
              (total_power - link_dynamic_power - link_leakage_power)
       << "%\n";
  cout << "- Leakage Portion:          "
       << (total_leakage_power - link_leakage_power) * 100 /
              (total_power - link_dynamic_power - link_leakage_power)
       << "%\n";
  cout << "-----------------------------------------\n";

  cout << "-----------------------------------------\n";
  cout << "- Application Total OCN Power Summary\n";
  cout << "- Completion Time (cycle):  " << net_energy_stats.tot_time << "\n";
  cout << "- Flit Widths:              " << channel_width << "\n";
  cout << "---\n";

  cout << "- Link Dynamic Power:       "
       << net_energy_stats.link_dynamic_energy / total_run_time << "\n";
  cout << "- Link Leakage Power:       "
       << net_energy_stats.link_leakage_energy / total_run_time << "\n";
  cout << "---\n";

  cout << "- Buffer Read Power:        "
       << net_energy_stats.buf_rd_energy / total_run_time << "\n";
  cout << "- Buffer Write Power:       "
       << net_energy_stats.buf_wt_energy / total_run_time << "\n";
  cout << "- Buffer Leakage Power:     "
       << net_energy_stats.buf_leakage_energy / total_run_time << "\n";
  cout << "---\n";

  cout << "- Switch Dynamic Power:     "
       << net_energy_stats.sw_dynamic_energy / total_run_time << "\n";
  cout << "- Switch Leakage Power:     "
       << net_energy_stats.sw_leakage_energy / total_run_time << "\n";
  cout << "---\n";

  cout << "- Crossbar Dynamic Power:   "
       << net_energy_stats.xbar_dynamic_energy / total_run_time << "\n";
  cout << "- Crossbar Leakage Power:   "
       << net_energy_stats.xbar_leakage_energy / total_run_time << "\n";
  cout << "---\n";

  cout << "- Clk Dynamic Power:        "
       << net_energy_stats.clk_dynamic_energy / total_run_time << "\n";
  cout << "- Clk Static Power:         "
       << net_energy_stats.clk_leakage_energy / total_run_time << "\n";
  cout << "---\n";

  cout << "- Power Gating Overhead:    "
       << net_energy_stats.power_gate_energy / total_run_time << "\n";
  cout << "---\n";

  cout << "- NoC Power (Including links):\n";
  cout << "- Total Dynamic Power:      "
       << net_energy_stats.tot_net_dynamic_energy / total_run_time << "\n";
  cout << "- Total Leakage Power:      "
       << net_energy_stats.tot_net_leakage_energy / total_run_time << "\n";
  cout << "- Total PowerGate Overhead: "
       << net_energy_stats.power_gate_energy / total_run_time << "\n";
  cout << "- Total Power:              "
       << net_energy_stats.tot_net_energy / total_run_time << "\n";
  cout << "- PG Overhea Portion:       "
       << net_energy_stats.power_gate_energy * 100 /
              net_energy_stats.tot_net_energy
       << "%\n";
  cout << "- Leakage Portion:          "
       << net_energy_stats.tot_net_leakage_energy * 100 /
              net_energy_stats.tot_net_energy
       << "%\n";
  cout << "---\n";

  cout << "- Routers Power (Excluding links):\n";
  cout << "- Total Dynamic Power:      "
       << net_energy_stats.tot_rt_dynamic_energy / total_run_time << "\n";
  cout << "- Total Leakage Power:      "
       << net_energy_stats.tot_rt_leakage_energy / total_run_time << "\n";
  cout << "- Total PowerGate Power:    "
       << net_energy_stats.power_gate_energy / total_run_time << "\n";
  cout << "- Total Power:              "
       << net_energy_stats.tot_rt_energy / total_run_time << "\n";
  cout << "- PG Overhead Portion:      "
       << net_energy_stats.power_gate_energy * 100 /
              net_energy_stats.tot_rt_energy
       << "%\n";
  cout << "- Leakage Portion:          "
       << net_energy_stats.tot_rt_leakage_energy * 100 /
              net_energy_stats.tot_rt_energy
       << "%\n";
  cout << "-----------------------------------------\n";
}
