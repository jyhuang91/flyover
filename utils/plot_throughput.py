#!/usr/bin/python

import sys
import numpy as np
import matplotlib.pyplot as plt
from easypyplot import pdf, barchart, color
from easypyplot import format as fmt


def main():

    traffic = 'uniform'
    off = 50

    dimensions = [6, 8, 10]
    schemes = [
        'baseline', 'rpa', 'rpc', 'rflov', 'gflov', 'opt_rflov', 'opt_gflov'
    ]
    paper_schemes = [
        'Baseline', 'RPA', 'RPC', 'rFLOV', 'gFLOV', 'rFLOVopt', 'gFLOVopt'
    ]
    injection_rates = []
    for i in range(1, 61):
        injection_rates.append("%.2f" % (float(i) / 100))

    latencies = []
    for d, dimension in enumerate(dimensions):
        latencies.append(
            np.zeros(
                (int(len(schemes)), int(len(injection_rates))),
                dtype=np.float))

    for d, dimension in enumerate(dimensions):
        for s, scheme in enumerate(schemes):
            for i, injection_rate in enumerate(injection_rates):
                filename = scheme + '/' + str(
                    dimension
                ) + 'dim/' + traffic + '_' + injection_rate + 'inj_' + str(
                    dimension) + 'dim_' + str(off) + 'off.log'

                infile = open(filename)
                for l, line in enumerate(infile):
                    if 'Packet latency average' in line and 'samples' in line:
                        line = line.split()
                        latencies[d][s][i] = line[4]
                        break

    # figure generation
    plt.rc('font', size=14)
    plt.rc('font', weight='bold')
    plt.rc('legend', fontsize=14)
    #linestyles = ['-', '-', '-', '--', '--']
    #markers = ['o', '^', 'd', 'v', 'D']
    #colors = ['#27408b', '#000000', '#ee0000', '#cd3278', '#451900']
    linestyles = ['-', '-.', '--', '-', '-', '--', '--']
    markers = ['o', 's', 'p', '^', 'd', 'v', 'D']
    colors = [
        '#27408b', '#c39d00', '#8b5742', '#000000', '#ee0000', '#cd3278',
        '#451900'
    ]

    for d, dimension in enumerate(dimensions):
        figname = traffic + '_' + str(dimension) + 'dim_50off_throughput.pdf'
        pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
        ax = fig.gca()
        for s, scheme in enumerate(paper_schemes):
            ax.plot(
                injection_rates,
                latencies[d][s, :],
                marker=markers[s],
                markersize=9,
                markeredgecolor=colors[s],
                color=colors[s],
                linestyle=linestyles[s],
                linewidth=2,
                label=scheme)
        ax.set_ylabel('Average Packet Latency (Cycles)')
        ax.set_xlabel('injection rate (flits/cycle/core)')
        ax.yaxis.grid(True, linestyle='--', color='black')
        hdls, lab = ax.get_legend_handles_labels()
        ax.legend(
            hdls,
            lab,
            loc='upper center',
            bbox_to_anchor=(0.5, 1.3),
            ncol=4,
            frameon=False,
            handletextpad=0.2,
            columnspacing=0.8)
        ax.set_ylim(0, 300)
        fig.subplots_adjust(top=0.8, bottom=0.2)
        pdf.plot_teardown(pdfpage, fig)

    #figname = traffic + injection_rate_name[injection_rate] + 'latency.pdf'
    #pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    #ax = fig.gca()
    #for s, scheme in enumerate(paper_schemes):
    #    ax.plot(
    #        off_percentile,
    #        paper_latency[s, :],
    #        marker=markers[s],
    #        markersize=9,
    #        markeredgecolor=colors[s],
    #        color=colors[s],
    #        linestyle=linestyles[s],
    #        linewidth=2,
    #        label=scheme)
    #ax.set_ylabel('Packet Latency (Cycles)')
    ##ax.set_xlabel('Fraction of Power-Gated Cores (%)')
    #xlab = 'Fraction of Power-Gated Cores (%), ' + injection_rate + ' flits/core/cycle'
    #ax.set_xlabel(xlab)
    #ax.yaxis.grid(True, linestyle='--', color='black')
    #hdls, lab = ax.get_legend_handles_labels()
    #ax.legend(
    #    hdls,
    #    lab,
    #    loc='upper center',
    #    bbox_to_anchor=(0.5, 1.3),
    #    ncol=3,
    #    frameon=False)
    ##ax.legend(loc='upper center', ncol=4, frameon=False)
    #if traffic == 'uniform':
    #    ax.set_ylim(25, 45)
    #else:
    #    ax.set_ylim(10, 30)
    #ax.set_xlim(0, 90)
    #fig.subplots_adjust(top=0.8, bottom=0.2)
    ##plt.tight_layout()
    #pdf.plot_teardown(pdfpage, fig)

    #figname = traffic + injection_rate_name[injection_rate] + 'dynamic_power.pdf'
    #pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    #ax = fig.gca()
    #for s, scheme in enumerate(paper_schemes):
    #    ax.plot(
    #        off_percentile,
    #        paper_dynamic_power[s, :],
    #        #router_dynamic_power[s, :],
    #        marker=markers[s],
    #        markersize=9,
    #        markeredgecolor=colors[s],
    #        color=colors[s],
    #        linestyle=linestyles[s],
    #        linewidth=2,
    #        label=scheme)
    #ax.set_ylabel('Dynamic Power (W)')
    ##ax.set_xlabel('Fraction of Power-Gated Cores (%)')
    #ax.set_xlabel(xlab)
    #ax.yaxis.grid(True, linestyle='--', color='black')
    #hdls, lab = ax.get_legend_handles_labels()
    #ax.legend(
    #    hdls,
    #    lab,
    #    loc='upper center',
    #    bbox_to_anchor=(0.5, 1.3),
    #    ncol=3,
    #    frameon=False)
    ##ax.legend(loc='upper center', ncol=4)
    #ax.set_xlim(0, 90)
    #if injection_rate == '0.02':
    #    ax.set_ylim(0, 0.3)
    #elif injection_rate == '0.08':
    #    ax.set_ylim(0, 0.8)
    #fig.subplots_adjust(top=0.8, bottom=0.2)
    #pdf.plot_teardown(pdfpage, fig)

    #figname = traffic + injection_rate_name[injection_rate] + 'total_power.pdf'
    #pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    #ax = fig.gca()
    #for s, scheme in enumerate(paper_schemes):
    #    ax.plot(
    #        off_percentile,
    #        paper_total_power[s, :],
    #        #router_total_power[s, :],
    #        marker=markers[s],
    #        markersize=9,
    #        markeredgecolor=colors[s],
    #        color=colors[s],
    #        linestyle=linestyles[s],
    #        linewidth=2,
    #        label=scheme)
    #ax.set_ylabel('Total Power (W)')
    ##ax.set_xlabel('Fraction of Power-Gated Cores (%)')
    #ax.set_xlabel(xlab)
    #ax.yaxis.grid(True, linestyle='--', color='black')
    #hdls, lab = ax.get_legend_handles_labels()
    #ax.legend(
    #    hdls,
    #    lab,
    #    loc='upper center',
    #    bbox_to_anchor=(0.5, 1.3),
    #    ncol=3,
    #    frameon=False)
    ##ax.legend(loc='upper center', ncol=4)
    #ax.set_xlim(0, 90)
    #if injection_rate == '0.02':
    #    ax.set_ylim(0, 1)
    #elif injection_rate == '0.08':
    #    ax.set_ylim(0, 1.4)
    #fig.subplots_adjust(top=0.8, bottom=0.2)
    #pdf.plot_teardown(pdfpage, fig)

    #figname = traffic + injection_rate_name[injection_rate] + 'static_power.pdf'
    ##figname = injection_rate + 'static_power.pdf'
    #pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    #ax = fig.gca()
    #for s, scheme in enumerate(paper_schemes):
    #    ax.plot(
    #        off_percentile,
    #        paper_static_power[s, :],
    #        marker=markers[s],
    #        markersize=9,
    #        markeredgecolor=colors[s],
    #        color=colors[s],
    #        linestyle=linestyles[s],
    #        linewidth=2,
    #        label=scheme)
    #ax.set_ylabel('Static Power (W)')
    #ax.set_xlabel('Fraction of Power-Gated Cores (%), ' + injection_rate +
    #              ' flits/cycle/core')
    #ax.yaxis.grid(True, linestyle='--', color='black')
    #hdls, lab = ax.get_legend_handles_labels()
    #ax.legend(
    #    hdls,
    #    lab,
    #    loc='upper center',
    #    bbox_to_anchor=(0.5, 1.3),
    #    ncol=3,
    #    frameon=False)
    #ax.set_xlim(0, 90)
    #fig.subplots_adjust(top=0.8, bottom=0.2)
    #pdf.plot_teardown(pdfpage, fig)

    #group_names = []
    #xticks = []
    #for o, off in enumerate(off_percentile):
    #    for s, scheme in enumerate(paper_schemes):
    #        group_names.append(scheme)
    #        xticks.append(o * (len(paper_schemes) + 1) + s)

    #colors = ['#ffffcc', '#a1dab4', '#41b6c4', '#2c7fb8', '#253494']
    #colors = ['#ca0020', '#f4a582', '#f7f7f7', '#92c5de', '#0571b0']
    #colors = ['#0570b0', '#000000', '#ffffff', '#fee0d2', '#f7fcf5']
    #figname = traffic + injection_rate_name[injection_rate] + 'lat_breakdown.pdf'
    #pdfpage, fig = pdf.plot_setup(figname, figsize=(12, 4), fontsize=14)
    #ax = fig.gca()
    #hdls = barchart.draw(
    #    ax,
    #    paper_latency_breakdown,
    #    group_names=group_names,
    #    entry_names=breakdown_comp,
    #    breakdown=True,
    #    xticks=xticks,
    #    width=0.8,
    #    colors=colors,
    #    legendloc='upper center',
    #    legendncol=5,
    #    xticklabelfontsize=11,
    #    xticklabelrotation=90)
    #ax.set_ylabel('Latency (Cycles)')
    #ax.set_xlabel(xlab)
    #ax.xaxis.set_label_coords(0.5, -0.55)
    #ax.yaxis.grid(True, linestyle='--')
    #ax.legend(
    #    hdls,
    #    breakdown_comp,
    #    loc='upper center',
    #    bbox_to_anchor=(0.5, 1.2),
    #    ncol=5,
    #    frameon=False,
    #    handletextpad=0.1,
    #    columnspacing=0.5)
    #fmt.resize_ax_box(ax, hratio=0.8)
    #ly = len(off_percentile)
    #scale = 1. / ly
    #ypos = -.5
    #pos = 0
    #for pos in xrange(ly + 1):
    #    lxpos = (pos + 0.5) * scale
    #    if pos < ly:
    #        ax.text(
    #            lxpos,
    #            ypos,
    #            off_percentile[pos],
    #            ha='center',
    #            transform=ax.transAxes)
    #        add_line(ax, pos * scale, ypos)
    #add_line(ax, 1, ypos)
    #fig.subplots_adjust(bottom=0.38)
    #pdf.plot_teardown(pdfpage, fig)

    #plt.show()


if __name__ == '__main__':
    main()
