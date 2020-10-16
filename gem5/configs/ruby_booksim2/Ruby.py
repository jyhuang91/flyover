# Copyright (c) 2012 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2006-2007 The Regents of The University of Michigan
# Copyright (c) 2009 Advanced Micro Devices, Inc.
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
#
# Authors: Brad Beckmann

import math
import m5
from m5.objects import *
from m5.defines import buildEnv
from m5.util import addToPath, fatal

addToPath('../topologies_booksim2')

def define_options(parser):
    # By default, ruby uses the simple timing cpu
    parser.set_defaults(cpu_type="timing")

    parser.add_option("--ruby-clock", action="store", type="string",
                      default='2GHz',
                      help="Clock for blocks running at Ruby system's speed")

    # Options related to cache structure
    parser.add_option("--ports", action="store", type="int", default=4,
                      help="used of transitions per cycle which is a proxy \
                            for the number of ports.")

    # ruby network options
    parser.add_option("--topology", type="string", default="Crossbar",
                 help="check src/mem/ruby/network/topologies for complete set")
    parser.add_option("--mesh-rows", type="int", default=1,
                      help="the number of rows in the mesh topology")
    parser.add_option("--garnet-network", type="choice",
                      choices=['fixed', 'flexible'], help="'fixed'|'flexible'")
    parser.add_option("--network-fault-model", action="store_true", default=False,
                      help="enable network fault model: see src/mem/ruby/network/fault_model/")
    # booksim2 options
    parser.add_option("--booksim-network", action="store_true", default=False,
                      help="use booksim network")
    parser.add_option("--booksim-config", action="store", type="string",
                      default='configs/ruby_booksim/gem5booksim.cfg',
                      help="path to booksim config file")
    parser.add_option("--noc-pg", action="store_true", default=False,
                      help="NoC power gating, create different core router \
                            map in topology")
    parser.add_option("--buffers-per-vc", type="int", default=5,
                      help="buffer depth per virtual channel")

    # ruby mapping options
    parser.add_option("--numa-high-bit", type="int", default=0,
                      help="high order address bit to use for numa mapping. " \
                           "0 = highest bit, not specified = lowest bit")

    # ruby sparse memory options
    parser.add_option("--use-map", action="store_true", default=False)
    parser.add_option("--map-levels", type="int", default=4)

    parser.add_option("--recycle-latency", type="int", default=10,
                      help="Recycle latency for ruby controller input buffers")

    parser.add_option("--random_seed", type="int", default=1234,
                      help="Used for seeding the random number generator")

    parser.add_option("--ruby_stats", type="string", default="ruby.stats")

    protocol = buildEnv['PROTOCOL']
    exec "import %s" % protocol
    eval("%s.define_options(parser)" % protocol)

def create_topology(controllers, options):
    """ Called from create_system in configs/ruby/<protocol>.py
        Must return an object which is a subclass of BaseTopology
        found in configs/topologies/BaseTopology.py
        This is a wrapper for the legacy topologies.
    """
    exec "import %s as Topo" % options.topology
    topology = eval("Topo.%s(controllers)" % options.topology)
    return topology

def create_system(options, system, piobus = None, dma_ports = []):

    system.ruby = RubySystem(no_mem_vec = options.use_map)
    ruby = system.ruby

    # Set the network classes based on the command line options
    if options.garnet_network == "fixed":
        NetworkClass = GarnetNetwork_d
        IntLinkClass = GarnetIntLink_d
        ExtLinkClass = GarnetExtLink_d
        RouterClass = GarnetRouter_d
        InterfaceClass = GarnetNetworkInterface_d

    elif options.garnet_network == "flexible":
        NetworkClass = GarnetNetwork
        IntLinkClass = GarnetIntLink
        ExtLinkClass = GarnetExtLink
        RouterClass = GarnetRouter
        InterfaceClass = GarnetNetworkInterface

    # booksim2
    elif options.booksim_network:
        NetworkClass = BookSimNetwork
        IntLinkClass = None
        ExtLinkClass = None
        RouterClass = None
        InterfaceClass = None

    else:
        NetworkClass = SimpleNetwork
        IntLinkClass = SimpleIntLink
        ExtLinkClass = SimpleExtLink
        RouterClass = Switch
        InterfaceClass = None

    # Instantiate the network object so that the controllers can connect to it.
    network = NetworkClass(ruby_system = ruby, topology = options.topology,
            routers = [], ext_links = [], int_links = [], netifs = [],
            use_booksim = options.booksim_network, ext_nodes = [])
    # booksim2, Jiayi 09/22/16
    if options.booksim_network:
        network.booksim_config = options.booksim_config
        network.outdir = m5.options.outdir
        network.buffers_per_vc = options.buffers_per_vc
        network.kary = options.mesh_rows
    ruby.network = network

    protocol = buildEnv['PROTOCOL']
    exec "import %s" % protocol
    try:
        (cpu_sequencers, dir_cntrls, topology) = \
             eval("%s.create_system(options, system, dma_ports, ruby)"
                  % protocol)
    except:
        print "Error: could not create sytem for ruby protocol %s" % protocol
        raise

    # Create a port proxy for connecting the system port. This is
    # independent of the protocol and kept in the protocol-agnostic
    # part (i.e. here).
    sys_port_proxy = RubyPortProxy(ruby_system = ruby)

    # Give the system port proxy a SimObject parent without creating a
    # full-fledged controller
    system.sys_port_proxy = sys_port_proxy

    # Connect the system port for loading of binaries etc
    system.system_port = system.sys_port_proxy.slave

    # Create the network topology
    if options.booksim_network:
        if options.noc_pg:
            topology.makeFLOVTopology(options, network)
        else:
            topology.makeBooksimTopology(options, network)
    else:
        topology.makeTopology(options, network, IntLinkClass, ExtLinkClass,
                RouterClass)

    if InterfaceClass != None:
        netifs = [InterfaceClass(id=i) for (i,n) in enumerate(network.ext_links)]
        network.netifs = netifs

    if options.network_fault_model:
        assert(options.garnet_network == "fixed")
        network.enable_fault_model = True
        network.fault_model = FaultModel()

    # Loop through the directory controlers.
    # Determine the total memory size of the ruby system and verify it is equal
    # to physmem.  However, if Ruby memory is using sparse memory in SE
    # mode, then the system should not back-up the memory state with
    # the Memory Vector and thus the memory size bytes should stay at 0.
    # Also set the numa bits to the appropriate values.
    total_mem_size = MemorySize('0B')

    ruby.block_size_bytes = options.cacheline_size
    block_size_bits = int(math.log(options.cacheline_size, 2))

    if options.numa_high_bit:
        numa_bit = options.numa_high_bit
    else:
        # if the numa_bit is not specified, set the directory bits as the
        # lowest bits above the block offset bits, and the numa_bit as the
        # highest of those directory bits
        dir_bits = int(math.log(options.num_dirs, 2))
        numa_bit = block_size_bits + dir_bits - 1

    for dir_cntrl in dir_cntrls:
        total_mem_size.value += dir_cntrl.directory.size.value
        dir_cntrl.directory.numa_high_bit = numa_bit

    phys_mem_size = sum(map(lambda r: r.size(), system.mem_ranges))
    assert(total_mem_size.value == phys_mem_size)
    ruby.mem_size = total_mem_size

    # Connect the cpu sequencers and the piobus
    if piobus != None:
        for cpu_seq in cpu_sequencers:
            cpu_seq.pio_master_port = piobus.slave
            cpu_seq.mem_master_port = piobus.slave

            if buildEnv['TARGET_ISA'] == "x86":
                cpu_seq.pio_slave_port = piobus.master

    ruby._cpu_ports = cpu_sequencers
    ruby.num_of_sequencers = len(cpu_sequencers)
    ruby.random_seed    = options.random_seed