# Copyright (c) 2014 Mark D. Hill and David A. Wood
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from ConfigParser import ConfigParser
import string, sys, subprocess, os, re
import json


# Shell command ignore CalledProcessError
def shell(command):
    try:
        output = subprocess.check_output(command, stderr=subprocess.STDOUT)
    except Exception, e:
        output = str(e.output)

    #print output
    return output


# Compile DSENT to generate the Python module and then import it.
# This script assumes it is executed from the gem5 root.
print("Attempting compilation")
from subprocess import call

src_dir = 'ext/dsent'
build_dir = 'build/ext/dsent'

if not os.path.exists(build_dir):
    os.makedirs(build_dir)
os.chdir(build_dir)

error = call(['cmake', '../../../%s' % src_dir])
if error:
    print("Failed to run cmake")
    exit(-1)

error = call(['make'])
if error:
    print("Failed to run make")
    exit(-1)

print("Compiled dsent")
os.chdir("../../../")
sys.path.append("build/ext/dsent")
import dsent


# Parse gem5 config.ini file for the configuration parameters related to
# the on-chip network.
def parseConfig(config_file):
    config = ConfigParser()
    if not config.read(config_file):
        print("ERROR: config file '", config_file, "' not found")
        sys.exit(1)

    if not config.has_section("system.ruby.network"):
        print("ERROR: Ruby network not found in '", config_file)
        sys.exit(1)

    if config.get("system.ruby.network", "type") != "BooksimNetwork":
        print("ERROR: Garnet network or Booksim network not used in '",
              config_file)
        sys.exit(1)

    router_id = config.get("system.ruby.network", "attached_router_id").split()
    attached_router_id = [int(s) for s in router_id if s.isdigit()]
    number_of_virtual_networks = config.getint("system.ruby.network",
                                               "number_of_virtual_networks")
    booksim_config = config.get("system.ruby.network", "booksim_config")

    vcs_per_vnet = config.getint("system.ruby.network", "vcs_per_vnet")

    buffers_per_vc = config.getint("system.ruby.network", "buffers_per_vc")

    flit_size_bits = 8 * config.getint("system.ruby.network", "flit_size")

    return (config, number_of_virtual_networks, vcs_per_vnet, buffers_per_vc,
            flit_size_bits, attached_router_id, booksim_config)


def getClock(obj, config):
    if config.get(obj, "type") == "SrcClockDomain":
        return config.getint(obj, "clock")

    if config.get(obj, "type") == "DerivedClockDomain":
        source = config.get(obj, "clk_domain")
        divider = config.getint(obj, "clk_divider")
        return getClock(source, config) / divider

    source = config.get(obj, "clk_domain")
    return getClock(source, config)


## Compute the power consumed by the given router
def computeRouterPowerAndArea(
        router, stats_file, config, number_of_virtual_networks, vcs_per_vnet,
        buffers_per_vc, attached_router_id, flit_size_bits, inj_rate):
    cycle_time_in_ps = getClock("system.ruby.clk_domain", config)
    frequency = 1000000000000 / cycle_time_in_ps
    num_ports = 4  # consider mesh

    for r in attached_router_id:
        if r == router:
            num_ports += 1

    power = dsent.computeRouterPowerAndArea(
        frequency, num_ports, num_ports, number_of_virtual_networks,
        vcs_per_vnet, buffers_per_vc, flit_size_bits, inj_rate)

    #print("router#" + str(router) + " Power: ", power)

    return power


## Compute the power consumed by the given link
def computeLinkPower(link, stats_file, config, sim_seconds, num_bits,
                     act_factor):
    cycle_time_in_ps = getClock("system.ruby.clk_domain", config)
    frequency = 1000000000000 / cycle_time_in_ps
    power = dsent.computeLinkPower(frequency, num_bits, act_factor)
    dyn = power[0][1] * 2
    static = power[1][1] * 2
    power_new = ((power[0][0], dyn), (power[1][0], static))
    power = power_new
    #print("%s Power: " % link, power)

    return power


def parseStats(stats_dir, config, router_config_file, link_config_file,
               attached_router_id, number_of_virtual_networks, vcs_per_vnet,
               buffers_per_vc, flit_size_bits, booksim_config):

    # Open the stats.txt file and parse it to for the required numbers
    # and the number of routers.
    stats_file = stats_dir + "/stats.txt"
    booksim_stats_file = stats_dir + "/booksimstats.json"
    try:
        stats_handle = open(stats_file, 'r')
        stats_handle.close()
    except IOError:
        print("Failed to open ", stats_file, " for reading")
        exit(-1)

    # Now parse the stats
    pattern = "sim_seconds"
    lines = string.split(
        subprocess.check_output(["grep", pattern, stats_file]), '\n', -1)
    assert len(lines) >= 1

    ## Assume that the first line is the one required
    [l1, l2, l3] = lines[0].partition(" ")
    l4 = l3.strip().partition(" ")
    simulation_length_in_seconds = float(l4[0])

    # get clock cycle time in ps
    cycle_time_in_ps = getClock("system.ruby.clk_domain", config)
    cycles = simulation_length_in_seconds * 1000000000000 / cycle_time_in_ps

    # Get router and link activities
    # router buffer reads: injection
    pattern = "router_buffer_reads"
    lines = string.split(
        subprocess.check_output(["grep", pattern, stats_file]), '\n', -1)
    assert len(lines) >= 2
    [l1, l2, l3] = lines[0].partition(" ")
    router_act = [int(s) for s in l3.split() if s.isdigit()]
    inject_rate = [act / cycles for act in router_act]
    # inject link activiy
    pattern = "inject_link_activity"
    lines = string.split(
        subprocess.check_output(["grep", pattern, stats_file]), '\n', -1)
    assert len(lines) >= 2
    [l1, l2, l3] = lines[0].partition(" ")
    inject_act = [int(s) for s in l3.split() if s.isdigit()]
    inject_link_rate = [act / cycles for act in inject_act]
    # eject link activiy
    pattern = "eject_link_activity"
    lines = string.split(
        subprocess.check_output(["grep", pattern, stats_file]), '\n', -1)
    assert len(lines) >= 2
    [l1, l2, l3] = lines[0].partition(" ")
    eject_act = [int(s) for s in l3.split() if s.isdigit()]
    eject_link_rate = [act / cycles for act in eject_act]
    # internal link activiy
    pattern = "int_link_activity"
    lines = string.split(
        subprocess.check_output(["grep", pattern, stats_file]), '\n', -1)
    assert len(lines) >= 2
    [l1, l2, l3] = lines[0].partition(" ")
    int_link_act = [int(s) for s in l3.split() if s.isdigit()]
    int_link_rate = [act / cycles for act in int_link_act]

    # Open the gem5booksim.cfg file and parse it to for the off routers.
    try:
        booksim_config_handle = open(booksim_config, 'r')
        booksim_config_handle.close()
    except IOError:
        print("Failed to open ", stats_file, " for reading")
        exit(-1)
    # get off routers
    pattern = "off_routers"
    lines = shell(["grep", pattern, booksim_config])
    off_routers = []
    if len(lines) >= 1:
        l1 = re.findall('\d+', lines)
        off_routers = map(int, l1)
    #print off_routers
    pattern = "network.flov_hops"
    lines = string.split(
        subprocess.check_output(["grep", pattern, stats_file]), '\n', -1)
    flov_hops = [int(s) for s in lines[0].split() if s.isdigit()]
    flov_hops = sum(flov_hops)
    #print flov_hops
    pattern = "network.hops"
    lines = string.split(
        subprocess.check_output(["grep", pattern, stats_file]), '\n', -1)
    hops = [int(s) for s in lines[0].split() if s.isdigit()]
    hops = sum(hops)
    #print hops

    # Initialize DSENT with a configuration file
    dsent.initialize(router_config_file)

    # Compute the power consumed by the routers
    with open(booksim_stats_file) as json_file:
        booksim_stats = json.load(json_file)
    router_dynamic_power = 0.0
    router_static_power = 0.0
    for (r, inj) in enumerate(inject_rate):
        if r == 0:
            inj = flov_hops / cycles + hops / cycles
        else:
            inj = 0
        rpower = computeRouterPowerAndArea(
            r, stats_file, config, number_of_virtual_networks, vcs_per_vnet,
            buffers_per_vc, attached_router_id, flit_size_bits, inj)
        router_dynamic_power += rpower[2][1]
        router_static_power += rpower[3][1] * booksim_stats["routers"]["router_{}".format(r)]["power-on-percentile"]

    # Finalize DSENT
    dsent.finalize()

    # Initialize DSENT with a configuration file
    dsent.initialize(link_config_file)

    # Compute the power consumed by the links
    link_dynamic_power = 0.0
    link_static_power = 0.0
    for (i, act_factor) in enumerate(inject_link_rate):
        link = "inject_link#" + str(i)
        lpower = computeLinkPower(link, stats_file, config,
                                  simulation_length_in_seconds, flit_size_bits,
                                  act_factor)
        link_dynamic_power += lpower[0][1]
        link_static_power += lpower[1][1]

    for (i, act_factor) in enumerate(eject_link_rate):
        link = "eject_link#" + str(i)
        lpower = computeLinkPower(link, stats_file, config,
                                  simulation_length_in_seconds, flit_size_bits,
                                  act_factor)
        link_dynamic_power += lpower[0][1]
        link_static_power += lpower[1][1]

    for (i, act_factor) in enumerate(int_link_rate):
        link = "internal_link#" + str(i)
        lpower = computeLinkPower(link, stats_file, config,
                                  simulation_length_in_seconds, flit_size_bits,
                                  act_factor)
        link_dynamic_power += lpower[0][1]
        link_static_power += lpower[1][1]

    # Finalize DSENT
    dsent.finalize()

    dynamic_power = router_dynamic_power + link_dynamic_power
    static_power = router_static_power + link_static_power
    dynamic_energy = dynamic_power * simulation_length_in_seconds
    static_energy = static_power * simulation_length_in_seconds
    print "total router dynamic power: " + str(router_dynamic_power)
    print "total router static power: " + str(router_static_power)
    print "total link dynamic power: " + str(link_dynamic_power)
    print "total link static power: " + str(link_static_power)
    print "network dynamic power: " + str(dynamic_power)
    print "network static power: " + str(static_power)
    print "\nNetwork Energy:"
    print "    Dynamic Energy: " + str(dynamic_energy)
    print "    Static Energy:  " + str(static_energy)

    return dynamic_power, static_power, dynamic_energy, static_energy


# Compute and return power and energy
def getPowerAndEnergy(sim_directory, router_config_file, link_config_file):
    config_file = sim_directory + "/config.ini"
    stats_file = sim_directory + "/stats.txt"

    (config, number_of_virtual_networks, vcs_per_vnet, buffers_per_vc,
     flit_size_bits, attached_router_id,
     booksim_config) = parseConfig(config_file)

    (dynamic_power, static_power, dynamic_energy, static_energy) = parseStats(
        sim_directory, config, router_config_file, link_config_file,
        attached_router_id, number_of_virtual_networks, vcs_per_vnet,
        buffers_per_vc, flit_size_bits, booksim_config)

    return dynamic_power, static_power, dynamic_energy, static_energy


# This script parses the config.ini and the stats.txt from a run and
# generates the power and the area of the on-chip network using DSENT
def main():
    if len(sys.argv) != 5:
        print("Usage: ", sys.argv[0], " <gem5 root directory> " \
              "<simulation directory> <router config file> <link config file>")
        exit(-1)

    print("WARNING: configuration files for DSENT and McPAT are separate. " \
          "Changes made to one are not reflected in the other.")

    (config, number_of_virtual_networks, vcs_per_vnet, buffers_per_vc,
     flit_size_bits, attached_router_id, booksim_config) = parseConfig(
         "%s/%s/config.ini" % (sys.argv[1], sys.argv[2]))

    parseStats("%s/%s" % (sys.argv[1], sys.argv[2]), config, sys.argv[3],
               sys.argv[4], attached_router_id, number_of_virtual_networks,
               vcs_per_vnet, buffers_per_vc, flit_size_bits, booksim_config)


if __name__ == "__main__":
    main()
