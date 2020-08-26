#!/usr/bin/python

import sys
import numpy as np
import matplotlib.pyplot as plt
from easypyplot import pdf, barchart, color
from easypyplot import format as fmt


def add_line(ax, xpos, ypos):
    line = plt.Line2D(
        [xpos, xpos], [0, ypos],
        transform=ax.transAxes,
        color='black',
        linewidth=1)
    line.set_clip_on(False)
    ax.add_line(line)


def main():

    traffic = sys.argv[1]
    injection_rate = sys.argv[2]

    injection_rate_name = {'0.02': '002', '0.08': '008'}

    schemes = [
        'baseline', 'rpa', 'rpc', 'nord', 'flov', 'opt_flov'
    ]
    paper_schemes = [
        'Baseline', 'RP', 'NoRD', 'FLOV', 'FLOV+'
    ]
    off_percentile = [10, 20, 30, 40, 50, 60, 70, 80]
    breakdown_comp = [
        'router latency', 'FLOV latency', 'link latency',
        'serialization latency', 'contention latency'
    ]
    breakdown_comp = [
        'router', 'FLOV', 'link',
        'serialization', 'contention'
    ]
    power_breakdown_comp = ['dynamic', 'static']

    latency = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    static_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    dynamic_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    total_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    flit_lat = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    hops = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    flov_hops = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    latency_breakdown = np.zeros(
        (len(breakdown_comp), int(len(off_percentile) * len(schemes))),
        dtype=np.float)
    power_breakdown = np.zeros(
        (len(power_breakdown_comp), int(len(off_percentile) * len(schemes))),
        dtype=np.float)
    router_static_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    router_dynamic_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    router_total_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    paper_latency = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    paper_static_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    paper_dynamic_power = np.zeros(
        (int(len(schemes)), int(len(off_percentile))), dtype=np.float)
    paper_latency_breakdown = np.zeros(
        (len(breakdown_comp), int(len(off_percentile) * len(paper_schemes))),
        dtype=np.float)
    power_power_breakdown = np.zeros(
        (len(power_breakdown_comp), int(len(off_percentile) * len(paper_schemes))),
        dtype=np.float)

    for s, scheme in enumerate(schemes):
        for o, off in enumerate(off_percentile):
            filename = scheme + '/' + traffic + '_' + injection_rate + 'inj_' + str(
                off) + 'off.log'

            infile = open(filename)
            is_router = False
            for l, line in enumerate(infile):
                if 'Packet latency average' in line and 'samples' in line:
                    line = line.split()
                    latency[s][o] = line[4]
                elif 'Flit latency average' in line and 'samples' in line:
                    line = line.split()
                    flit_lat[s][o] = line[4]
                elif 'Hops average' in line and 'samples' in line:
                    line = line.split()
                    hops[s][o] = line[3]
                elif 'FLOV hops' in line and 'samples' in line:
                    line = line.split()
                    flov_hops[s][o] = line[4]
                elif 'Total Dynamic Power' in line and is_router == False:
                    line = line.split()
                    dynamic_power[s][o] = line[4]
                    power_breakdown[0][o * len(schemes) + s] = line[4]
                elif 'Total Leakage Power' in line and is_router == False:
                    line = line.split()
                    static_power[s][o] = line[4]
                    power_breakdown[1][o * len(schemes) + s] = line[4]
                elif 'Total Power:' in line and is_router == False:
                    is_router = True
                    line = line.split()
                    total_power[s][o] = line[3]
                elif 'Total Dynamic Power' in line and is_router == True:
                    line = line.split()
                    router_dynamic_power[s][o] = line[4]
                elif 'Total Leakage Power' in line and is_router == True:
                    line = line.split()
                    router_static_power[s][o] = line[4]
                elif 'Total Power:' in line and is_router == True:
                    line = line.split()
                    router_total_power[s][o] = line[3]

            zeroload_lat = 0
            latency_breakdown[0][o * len(schemes)
                                 + s] = hops[s][o] * 3  # router_lat
            zeroload_lat += latency_breakdown[0][o * len(schemes) + s]
            latency_breakdown[1][o * len(schemes)
                                 + s] = flov_hops[s][o]  # flov_lat
            zeroload_lat += latency_breakdown[1][o * len(schemes) + s]
            latency_breakdown[
                2][o * len(schemes)
                   + s] = flov_hops[s][o] + hops[s][o] + 1  # link_lat
            zeroload_lat += latency_breakdown[2][o * len(schemes) + s]
            latency_breakdown[3][o * len(schemes) + s] = 4  # serilized_lat
            zeroload_lat += latency_breakdown[3][o * len(schemes) + s]
            latency_breakdown[
                4][o * len(schemes)
                   + s] = latency[s][o] - zeroload_lat  # contention

    paper_latency = latency[[0, 1, 3, 4, 5], :]
    paper_dynamic_power = dynamic_power[[0, 1, 3, 4, 5], :]
    paper_static_power = static_power[[0, 1, 3, 4, 5], :]
    paper_total_power = total_power[[0, 1, 3, 4, 5], :]
    # select aggresive or conservative for router parking
    del_cols = []
    for o, off in enumerate(off_percentile):
        if total_power[1][o] > total_power[2][o]:
            paper_latency[1][o] = latency[2][o]
            paper_dynamic_power[1][o] = dynamic_power[2][o]
            paper_static_power[1][o] = static_power[2][o]
            paper_total_power[1][o] = total_power[2][o]
            del_cols.append(o * len(schemes) + 1)
        else:
            del_cols.append(o * len(schemes) + 2)
    #print latency
    #print paper_latency
    #print static_power
    data = [list(i) for i in zip(*latency_breakdown)]
    data = np.array(data, dtype=np.float)
    data = np.transpose(data)
    data = np.delete(data, del_cols, 1)
    paper_latency_breakdown = np.transpose(data)
    data = [list(i) for i in zip(*power_breakdown)]
    data = np.array(data, dtype=np.float)
    data = np.transpose(data)
    data = np.delete(data, del_cols, 1)
    paper_power_breakdown = np.transpose(data)

    # figure generation
    plt.rc('font', size=14)
    #plt.rc('font', weight='bold')
    plt.rc('legend', fontsize=14)
    #linestyles = ['-', '--', '--', '--', '-.', '-.']
    linestyles = ['-', '--', '-', '-', '--', '--']
    markers = ['o', 's', '^', 'd', 'v', 'D']
    #colors = ['r', 'g', 'b', 'y', 'm', 'k']
    colors = ['#27408b', '#8b5742', '#000000', '#ee0000', '#cd3278', '#451900']
    # matlab colors
    colors = ['#b7312c', '#f2a900', '#00a9e0', '#004b87', '#715091', '#636569',
            '#0076a8', '#d78825']
    markers = ['x', 'o', '^', 's', 'p', 'v', 'D', 'x']
    linestyles = ['-', '-', '-', '-', '-']

    figname = traffic + injection_rate_name[injection_rate] + 'latency.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    for s, scheme in enumerate(paper_schemes):
        ax.plot(
            off_percentile,
            paper_latency[s, :],
            marker=markers[s],
            markersize=9,
            markeredgewidth=2,
            fillstyle='none',
            markeredgecolor=colors[s],
            color=colors[s],
            linestyle=linestyles[s],
            linewidth=2,
            label=scheme)
    ax.set_ylabel('Packet Latency (Cycles)')
    #ax.set_xlabel('Fraction of Power-Gated Cores (%)')
    xlab = 'Fraction of Power-Gated Cores (%), ' + injection_rate + ' flits/core/cycle'
    ax.set_xlabel(xlab)
    ax.yaxis.grid(True, linestyle='--', color='black')
    hdls, lab = ax.get_legend_handles_labels()
    ax.legend(
        hdls,
        lab,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.2),
        ncol=len(paper_schemes),
        frameon=False)
    #ax.legend(loc='upper center', ncol=4, frameon=False)
    #if traffic == 'uniform':
    #    ax.set_ylim(25, 45)
    #else:
    #    ax.set_ylim(15, 35)
    ax.set_xlim(0, 90)
    fig.subplots_adjust(top=0.85, bottom=0.2)
    #plt.tight_layout()
    pdf.plot_teardown(pdfpage, fig)

    group_names = []
    xticks = []
    for o, off in enumerate(off_percentile):
        for s, scheme in enumerate(paper_schemes):
            group_names.append(scheme)
            xticks.append(o * (len(paper_schemes) + 1) + s)

    # normalized power breakdown
    colors = ['#a63603','#fee6ce']
    figname = traffic + injection_rate_name[injection_rate] + 'power_breakdown.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    ax2 = ax.twinx() # for normalized static power, must put it here for the look
    hdls = barchart.draw(
        ax,
        paper_power_breakdown,
        group_names=group_names,
        entry_names=power_breakdown_comp,
        breakdown=True,
        xticks=xticks,
        width=0.8,
        colors=colors,
        legendloc='upper center',
        legendncol=5,
        xticklabelfontsize=11,
        xticklabelrotation=90)
    ax.set_ylabel('Power (Watts)')
    ax.set_xlabel(xlab)
    ax.xaxis.set_label_coords(0.5, -0.55)
    ax.yaxis.grid(True, linestyle='--')
    fmt.resize_ax_box(ax, hratio=0.8)
    ly = len(off_percentile)
    scale = 1. / ly
    ypos = -.5
    pos = 0
    for pos in xrange(ly + 1):
        lxpos = (pos + 0.5) * scale
        if pos < ly:
            ax.text(
                lxpos,
                ypos,
                off_percentile[pos],
                ha='center',
                transform=ax.transAxes)
            add_line(ax, pos * scale, ypos)
    add_line(ax, 1, ypos)
    # add static power at secondary axis
    xs = []
    p = 0.0
    for g in range(len(off_percentile)):
        xs.append([])
        for pos in range(len(paper_schemes)):
            xs[g].append(p)
            p = p + 1
        p = p + 1
    data = [list(i) for i in zip(*paper_static_power)]
    data = np.array(data, dtype=np.float64)
    ax2.set_ylabel('Static Power (Watts)')
    ax2.set_ylim(0, 0.6)
    for i in range(len(off_percentile)):
        tmp = ax2.plot(xs[i],
                data[i],
                '-o',
                markersize=6,
                color='#004b87',
                markeredgecolor='#004b87')
        if i == 0:
            hdls += tmp
    ax.legend(
        hdls,
        power_breakdown_comp + ['static power'],
        loc='upper center',
        bbox_to_anchor=(0.5, 1.2),
        ncol=len(power_breakdown_comp) + 1,
        frameon=False,
        handletextpad=1,
        columnspacing=2)
    fig.subplots_adjust(bottom=0.38)
    pdf.plot_teardown(pdfpage, fig)

    # latency breakdown
    #colors = ['#0570b0', '#000000', '#ffffff', '#fee0d2', '#f7fcf5']
    #colors = ['#2f5597', '#000000', '#ffffff', '#f4b183', '#e2f0d9']
    #colors = ['#08519c','#3182bd','#6baed6','#bdd7e7','#eff3ff']
    colors = ['#08519c','#bdd7e7','#6baed6','#3182bd','#eff3ff']
    figname = traffic + injection_rate_name[injection_rate] + 'lat_breakdown.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    hdls = barchart.draw(
        ax,
        paper_latency_breakdown,
        group_names=group_names,
        entry_names=breakdown_comp,
        breakdown=True,
        xticks=xticks,
        width=0.8,
        colors=colors,
        legendloc='upper center',
        legendncol=5,
        xticklabelfontsize=11,
        xticklabelrotation=90)
    ax.set_ylabel('Latency (Cycles)')
    ax.set_xlabel(xlab)
    ax.xaxis.set_label_coords(0.5, -0.55)
    ax.yaxis.grid(True, linestyle='--')
    ax.legend(
        hdls,
        breakdown_comp,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.2),
        ncol=5,
        frameon=False,
        handletextpad=0.1,
        columnspacing=0.5)
    fmt.resize_ax_box(ax, hratio=0.8)
    ly = len(off_percentile)
    scale = 1. / ly
    ypos = -.5
    pos = 0
    for pos in xrange(ly + 1):
        lxpos = (pos + 0.5) * scale
        if pos < ly:
            ax.text(
                lxpos,
                ypos,
                off_percentile[pos],
                ha='center',
                transform=ax.transAxes)
            add_line(ax, pos * scale, ypos)
    add_line(ax, 1, ypos)
    fig.subplots_adjust(bottom=0.38)
    pdf.plot_teardown(pdfpage, fig)

    '''
    figname = traffic + injection_rate_name[injection_rate] + 'dynamic_power.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    for s, scheme in enumerate(paper_schemes):
        ax.plot(
            off_percentile,
            paper_dynamic_power[s, :],
            #router_dynamic_power[s, :],
            marker=markers[s],
            markersize=9,
            markeredgewidth=2,
            fillstyle='none',
            markeredgecolor=colors[s],
            color=colors[s],
            linestyle=linestyles[s],
            linewidth=2,
            label=scheme)
    ax.set_ylabel('Dynamic Power (W)')
    #ax.set_xlabel('Fraction of Power-Gated Cores (%)')
    ax.set_xlabel(xlab)
    ax.yaxis.grid(True, linestyle='--', color='black')
    hdls, lab = ax.get_legend_handles_labels()
    ax.legend(
        hdls,
        lab,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.3),
        ncol=3,
        frameon=False)
    #ax.legend(loc='upper center', ncol=4)
    ax.set_xlim(0, 90)
    if injection_rate == '0.02':
        ax.set_ylim(0, 0.3)
    elif injection_rate == '0.08':
        ax.set_ylim(0, 0.8)
    fig.subplots_adjust(top=0.8, bottom=0.2)
    pdf.plot_teardown(pdfpage, fig)

    figname = traffic + injection_rate_name[injection_rate] + 'total_power.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    for s, scheme in enumerate(paper_schemes):
        ax.plot(
            off_percentile,
            paper_total_power[s, :],
            #router_total_power[s, :],
            marker=markers[s],
            markersize=9,
            markeredgewidth=2,
            fillstyle='none',
            markeredgecolor=colors[s],
            color=colors[s],
            linestyle=linestyles[s],
            linewidth=2,
            label=scheme)
    ax.set_ylabel('Total Power (W)')
    #ax.set_xlabel('Fraction of Power-Gated Cores (%)')
    ax.set_xlabel(xlab)
    ax.yaxis.grid(True, linestyle='--', color='black')
    hdls, lab = ax.get_legend_handles_labels()
    ax.legend(
        hdls,
        lab,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.3),
        ncol=3,
        frameon=False)
    #ax.legend(loc='upper center', ncol=4)
    ax.set_xlim(0, 90)
    if injection_rate == '0.02':
        ax.set_ylim(0, 1)
    elif injection_rate == '0.08':
        ax.set_ylim(0, 1.4)
    fig.subplots_adjust(top=0.8, bottom=0.2)
    pdf.plot_teardown(pdfpage, fig)

    figname = traffic + injection_rate_name[injection_rate] + 'static_power.pdf'
    #figname = injection_rate + 'static_power.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    for s, scheme in enumerate(paper_schemes):
        ax.plot(
            off_percentile,
            paper_static_power[s, :],
            marker=markers[s],
            markersize=9,
            markeredgewidth=2,
            fillstyle='none',
            markeredgecolor=colors[s],
            color=colors[s],
            linestyle=linestyles[s],
            linewidth=2,
            label=scheme)
    ax.set_ylabel('Static Power (W)')
    ax.set_xlabel('Fraction of Power-Gated Cores (%), ' + injection_rate +
                  ' flits/cycle/core')
    ax.yaxis.grid(True, linestyle='--', color='black')
    hdls, lab = ax.get_legend_handles_labels()
    ax.legend(
        hdls,
        lab,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.3),
        ncol=3,
        frameon=False)
    ax.set_xlim(0, 90)
    fig.subplots_adjust(top=0.8, bottom=0.2)
    pdf.plot_teardown(pdfpage, fig)
    '''

    #plt.show()


if __name__ == '__main__':
    main()
