#!/usr/bin/python

import os
import sys
import math
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as plticker
from easypyplot import pdf, barchart, color
from easypyplot import format as fmt
from mpl_toolkits.axes_grid1.inset_locator import zoomed_inset_axes
from mpl_toolkits.axes_grid1.inset_locator import mark_inset


def main():

    traffic = 'uniform'
    off = 50

    dimensions = [4, 6, 8, 10, 20]
    schemes = [
        'baseline', 'rp', 'nord', 'flov', 'opt_flov'
    ]
    paper_schemes = [
        'Baseline', 'RP', 'NoRD', 'FLOV', 'FLOV+'
    ]
    rp_schemes = ['rpa', 'rpc', 'norp']
    injection_rates = []
    for i in range(1, 101, 1):
        injection_rates.append("%.2f" % (float(i) / 100))

    saturations = np.zeros((int(len(dimensions)), int(len(schemes))), dtype=np.float)
    latencies = []
    powers = []
    for d, dimension in enumerate(dimensions):
        latencies.append(
            np.zeros(
                (int(len(schemes)), int(len(injection_rates))),
                dtype=np.float))
        powers.append(
            np.zeros(
                (int(len(schemes)), int(len(injection_rates))),
                dtype=np.float))

    for d, dimension in enumerate(dimensions):
        for s, scheme in enumerate(schemes):
            for i, injection_rate in enumerate(injection_rates):
                if schemes[s] == 'rp':
                    rp_idx = 0
                    #power = None
                    latency = None
                    for j, rp in enumerate(rp_schemes):
                        rpfile = rp + '/' + str(dimension) + 'dim/' + traffic + '_' + injection_rate + 'inj_' + str(dimension) + 'dim_' + str(off) + 'off.log'

                        if os.path.exists(rpfile):
                            infile = open(rpfile)
                            for l, line in enumerate(infile):
                                if 'Packet latency average' in line and 'samples' in line:
                                    line = line.split()
                                    if latency == None:
                                        rp_idx = j
                                        latency = float(line[4])
                                    elif latency > 1.25 * float(line[4]):
                                        rp_idx = j
                                        latency = float(line[4])
                                    break
                                #if 'Total Power:' in line:
                                #    line = line.split()
                                #    if power == None:
                                #        rp_idx = j
                                #        power = line[3]
                                #    elif line[3] < power:
                                #        rp_idx = j
                                #        power = line[3]
                                #    break
                    scheme = rp_schemes[rp_idx]

                filename = scheme + '/' + str(
                    dimension
                ) + 'dim/' + traffic + '_' + injection_rate + 'inj_' + str(
                    dimension) + 'dim_' + str(off) + 'off.log'

                if os.path.exists(filename):
                    infile = open(filename)
                    for l, line in enumerate(infile):
                        if 'Packet latency average' in line and 'samples' in line:
                            line = line.split()
                            latencies[d][s][i] = float(line[4])
                            if float(line[4]) > 500 and saturations[d][s] == 0:
                                saturations[d][s] = float(injection_rate)
                        if 'Total Power:' in line:
                            line = line.split()
                            powers[d][s][i] = float(line[3])
                            break
                else:
                    latencies[d][s][i] = float(1000)

    paper_inj_rates = []
    paper_indices = []
    for d, dimension in enumerate(dimensions):
        paper_inj_rates.append([])
        paper_indices.append([])
        for s, scheme in enumerate(schemes):
            saturation = int(math.ceil(saturations[d][s] * 100))
            paper_inj_rates[d].append([])
            paper_inj_rates[d][s].append("0.01")
            gap = 5
            if dimension == 20:
                gap = 2
            for i in range(gap, saturation - 3, gap):
                paper_inj_rates[d][s].append("%.2f" % (float(i) / 100))
            for i in range(saturation - 3, saturation + 1, 1):
                paper_inj_rates[d][s].append("%.2f" % (float(i) / 100))
            inj_rate_set = set(paper_inj_rates[d][s])
            paper_indices[d].append([i for i, ele in enumerate(injection_rates) if ele in inj_rate_set])
            paper_inj_rates[d][s] = [float(rate) for i, rate in enumerate(paper_inj_rates[d][s])]

    # figure generation
    plt.rc('font', size=26)
    #plt.rc('font', weight='bold')
    plt.rc('legend', fontsize=20)
    #linestyles = ['-', '-', '-', '--', '--']
    #markers = ['o', '^', 'd', 'v', 'D']
    #colors = ['#27408b', '#000000', '#ee0000', '#cd3278', '#451900']
    linestyles = ['-', '-.', '--', '-', '-', '--', '--', '-']
    markers = ['o', 's', 'p', '^', 'd', 'v', 'D', 'x']
    colors = [
        '#27408b', '#c39d00', '#8b5742', '#000000', '#ee0000', '#cd3278',
        '#451900', '#66c2a4'
    ]
    # matlab colors
    colors = ['#b7312c', '#f2a900', '#48a23f', '#00a9e0', '#715091', '#636569',
            '#0076a8', '#d78825', '#004b87']
    colors = ['#b7312c', '#f2a900', '#48a23f', '#00a9e0', '#004b87', '#715091', '#636569',
            '#0076a8', '#d78825']
    markers = ['x', 'o', 'd', '^', 's', 'p', 'v', 'D', 'x']
    linestyles = ['-', '-', '-', '-', '-', '-', '-']

    for d, dimension in enumerate(dimensions):
        figname = traffic + '_' + str(dimension) + 'dim_50off_throughput.pdf'
        pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 8), fontsize=28)
        ax = fig.gca()
        for s, scheme in enumerate(paper_schemes):
            ax.plot(
                #injection_rates,
                #latencies[d][s, :],
                paper_inj_rates[d][s],
                latencies[d][s, paper_indices[d][s]],
                marker=markers[s],
                markersize=9,
                markeredgewidth=2,
                markeredgecolor=colors[s],
                fillstyle='none',
                color=colors[s],
                linestyle=linestyles[s],
                linewidth=2,
                label=scheme)
        ax.set_ylabel('Average Packet Latency (Cycles)')
        ax.set_xlabel('injection rate (flits/cycle/core)')
        ax.yaxis.grid(True, linestyle='--', color='black')
        hdls, lab = ax.get_legend_handles_labels()
        if dimension != 20:
            legend = ax.legend(
                hdls,
                lab,
                loc='upper left',
                #bbox_to_anchor=(0, 1.2),
                ncol=1,
                frameon=True,
                fontsize=22,
                handletextpad=0.5,
                columnspacing=1)
        else:
            legend = ax.legend(
                hdls,
                lab,
                loc='upper left',
                bbox_to_anchor=(0.16, 1),
                ncol=1,
                frameon=True,
                fontsize=22,
                handletextpad=0.5,
                columnspacing=1)
        legend.get_frame().set_edgecolor('white')
        ax.set_ylim(0, 500)
        fig.subplots_adjust(top=0.85, bottom=0.2, left=0.15)
        pdf.plot_teardown(pdfpage, fig)

    for d, dimension in enumerate(dimensions):
        figname = traffic + '_' + str(dimension) + 'dim_50off_power.pdf'
        pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 8), fontsize=28)
        ax = fig.gca()
        if dimension == 4:
            axins = zoomed_inset_axes(ax, 1.8, loc=4) # zoom-factor: 1.8, location: lower-right
        elif dimension == 20:
            axins = zoomed_inset_axes(ax, 1.5, loc=4) # zoom-factor: 2, location: lower-right
        else:
            axins = zoomed_inset_axes(ax, 2, loc=4) # zoom-factor: 2, location: lower-right
        for s, scheme in enumerate(paper_schemes):
            ax.plot(
                #injection_rates,
                #powers[d][s, :],
                paper_inj_rates[d][s][0:-1],
                powers[d][s, paper_indices[d][s][0:-1]],
                marker=markers[s],
                markersize=9,
                markeredgewidth=2,
                markeredgecolor=colors[s],
                fillstyle='none',
                color=colors[s],
                linestyle=linestyles[s],
                linewidth=2,
                label=scheme)
            axins.plot(
                #injection_rates,
                #powers[d][s, :],
                paper_inj_rates[d][s][0:-1],
                powers[d][s, paper_indices[d][s][0:-1]],
                marker=markers[s],
                markersize=9,
                markeredgewidth=2,
                markeredgecolor=colors[s],
                fillstyle='none',
                color=colors[s],
                linestyle=linestyles[s],
                linewidth=2,
                label=scheme)
        ax.set_ylabel('NoC Power (Watts)')
        ax.set_xlabel('injection rate (flits/cycle/core)')
        ax.yaxis.grid(True, linestyle='--', color='black')
        if dimension == 10:
            ax.yaxis.set_major_formatter(plticker.FormatStrFormatter('%.1f'))
        hdls, lab = ax.get_legend_handles_labels()
        legend = ax.legend(
            hdls,
            lab,
            #loc='upper center',
            #bbox_to_anchor=(0.5, 1.2),
            #ncol=5,
            loc='upper left',
            ncol=1,
            frameon=True,
            fontsize=20,
            handletextpad=0.5,
            columnspacing=1)
        legend.get_frame().set_edgecolor('white')
        if dimension == 4:
            x1, x2, y1, y2 = 0.1, 0.3, 0.14, 0.35 # specify the limits
            loc = plticker.MultipleLocator(base=0.05)
            axins.yaxis.set_major_locator(loc)
        elif dimension == 6:
            x1, x2, y1, y2 = 0, 0.15, 0.2, 0.8 # specify the limits
            loc = plticker.MultipleLocator(base=0.15)
            axins.yaxis.set_major_locator(loc)
        elif dimension == 8:
            x1, x2, y1, y2 = 0, 0.15, 0.3, 1.5 # specify the limits
            loc = plticker.MultipleLocator(base=0.3)
            axins.yaxis.set_major_locator(loc)
        elif dimension == 10:
            x1, x2, y1, y2 = 0, 0.15, 0.3, 2 # specify the limits
            loc = plticker.MultipleLocator(base=0.5)
            axins.yaxis.set_major_locator(loc)
        elif dimension == 20:
            x1, x2, y1, y2 = 0, 0.10, 2.5, 10 # specify the limits
            loc = plticker.MultipleLocator(base=2)
            axins.yaxis.set_major_locator(loc)
        axins.set_xlim(x1, x2) # apply the x-limits
        axins.set_ylim(y1, y2) # apply the y-limits
        axins.yaxis.grid(True, linestyle='--', color='black')
        axins.xaxis.set_ticklabels([])
        axins.yaxis.set_ticklabels([])
        axins.xaxis.set_ticks_position('none')
        axins.yaxis.set_ticks_position('none')
        mark_inset(ax, axins, loc1=2, loc2=3, fc="none", lw=1)
        fig.subplots_adjust(top=0.85, bottom=0.2, left=0.15)
        pdf.plot_teardown(pdfpage, fig)

    #plt.show()


if __name__ == '__main__':
    main()
