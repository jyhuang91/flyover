from m5.params import *
from m5.proxy import *
from Network import RubyNetwork
from ClockedObject import ClockedObject

class BookSimNetwork(RubyNetwork):
    type = 'BookSimNetwork'
    cxx_header = "mem/ruby/network/booksim2/BookSimNetwork.hh"

    attached_router_id = VectorParam.Int("Node router association map")
    booksim_config = Param.String("configs/ruby_booksim/gem5booksim.cfg",
                                  "path to booksim config file")
    outdir = Param.String("", "path of stat output directory")
    flit_size = Param.Int(16, "network flit size in bytes (ni and link)")
    vcs_per_vnet = Param.Int(4, "virtual channels per virtual network");

    buffers_per_vc = Param.UInt32(5, "buffers per virtual channel");
