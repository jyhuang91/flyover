#!/usr/bin/python

import sys
import numpy as np
import matplotlib.pyplot as plt
from easypyplot import pdf, barchart, color
from easypyplot import format as fmt


def main():

    traffic = 'uniform' #sys.argv[1]
    injection_rate = str(0.08) #sys.argv[2]

    schemes = [
        'baseline', 'rpa', 'rpa', 'rflov', 'flov', 'opt_rflov', 'opt_flov'
    ]
    paper_schemes = [
        'Baseline', 'RP-Tor', 'RP-UR', 'rFLOV', 'gFLOV', 'rFLOVopt', 'gFLOVopt'
    ]
    off_percentile = [10, 20, 30, 40, 50, 60, 70, 80]

    static_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    paper_static_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)

    for s, scheme in enumerate(schemes):
        for o, off in enumerate(off_percentile):
            if off == 20 and paper_schemes[s] == 'RP-Tor':
                filename = 'rpc/tornado_' + injection_rate + 'inj_' + str(off) + 'off.log'
            elif off == 20 and paper_schemes[s] == 'RP-UR':
                filename = 'rpc/uniform_' + injection_rate + 'inj_' + str(off) + 'off.log'
            elif off == 30 and paper_schemes[s] == 'RP-UR':
                filename = 'rpc/uniform_' + injection_rate + 'inj_' + str(off) + 'off.log'
            else:
                filename = scheme + '/' + traffic + '_' + injection_rate + 'inj_' + str(off) + 'off.log'

            infile = open(filename)
            for l, line in enumerate(infile):
                if 'Total Leakage Power' in line:
                    line = line.split()
                    static_power[s][o] = line[4]
                    break

    # figure generation
    plt.rc('font', size=14)
    plt.rc('font', weight='bold')
    plt.rc('legend', fontsize=14)
    linestyles = ['-', '-.', '--', '-', '-', '--', '--']
    markers = ['o', 's', 'p', '^', 'd', 'v', 'D']
    colors = ['#27408b', '#c39d00', '#8b5742', '#000000', '#ee0000', '#cd3278', '#451900']

    figname = injection_rate + 'static_power.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    for s, scheme in enumerate(paper_schemes):
        ax.plot(
            off_percentile,
            static_power[s, :],
            marker=markers[s],
            markersize=9,
            markeredgecolor=colors[s],
            color=colors[s],
            linestyle=linestyles[s],
            linewidth=2,
            label=scheme)
    ax.set_ylabel('Static Power (W)')
    ax.set_xlabel('Fraction of Power-Gated Cores (%), 0.08 flits/cycle/core')
    ax.yaxis.grid(True, linestyle='--', color='black')
    hdls, lab = ax.get_legend_handles_labels()
    ax.legend(
        hdls,
        lab,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.3),
        ncol=4,
        frameon=False)
    ax.set_xlim(0, 90)
    fig.subplots_adjust(top=0.8, bottom=0.2)
    pdf.plot_teardown(pdfpage, fig)


if __name__ == '__main__':
    main()
