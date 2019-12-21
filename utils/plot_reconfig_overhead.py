#!/usr/bin/python

import sys
import numpy as np
import matplotlib.pyplot as plt
from easypyplot import pdf, color
from easypyplot import format as fmt


def main():

    schemes = ['RP', 'gFLOV']
    latencies = []

    filename = '../results/reconfig/latency.txt'
    infile = open(filename)
    for l, line in enumerate(infile):
        if 'time' in line:
            line = line.split()
            cycles = [int(cycle) for cycle in line[1:-1]]
        elif 'RP' in line:
            line = line.split()
            latencies.append([float(latency) for latency in line[1:-1]])
        elif 'gFLOV' in line:
            line = line.split()
            #latencies[1] = [float(latency) for latency in line[1:-1]]
            latencies.append([float(latency) for latency in line[1:-1]])

    # figure generation
    plt.rc('font', size=14)
    plt.rc('font', weight='bold')
    plt.rc('legend', fontsize=14)
    colors = ['#8b5742', '#ee0000']
    linestyles = ['-', '--']

    figname = 'reconfig_overhead.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    for s, scheme in enumerate(schemes):
        ax.plot(
            cycles,
            latencies[s],
            color=colors[s],
            linestyle=linestyles[s],
            linewidth=2,
            label=scheme)
    ax.set_ylabel('Packet Latency (Cycles)')
    ax.set_xlabel('Timeline (Cycles)')
    ax.yaxis.grid(True, linestyle='--', color='black')
    hdls, lab = ax.get_legend_handles_labels()
    ax.legend(
        hdls,
        lab,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.2),
        ncol=2,
        frameon=False)
    fig.subplots_adjust(top=0.8, bottom=0.2)
    pdf.plot_teardown(pdfpage, fig)


if __name__ == '__main__':
    main()
