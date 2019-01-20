#!/usr/bin/python

import sys
import os
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats
from easypyplot import pdf, barchart, color
from easypyplot import format as fmt
sys.path.append(os.path.dirname(os.path.abspath(__file__)) + "/util")
import flov_noc_power_area as power_model


def add_line(ax, xpos, ypos):
    line = plt.Line2D(
        [xpos, xpos], [0, ypos],
        transform=ax.transAxes,
        color='black',
        linewidth=1)
    line.set_clip_on(False)
    ax.add_line(line)


def main():

    schemes = [
        'baseline', 'rpc', 'rpa', 'rflov', 'flov', 'opt_rflov', 'opt_flov'
    ]
    paper_schemes = [
        'Baseline', 'RP', 'rFLOV', 'gFLOV', 'rFLOVopt', 'gFLOVopt'
    ]
    #benchmarks = ['blackscholes', 'bodytrack']
    #speedup_xlabels = ['blackscholes', 'bodytrack', 'gmean']
    #energy_xlabels = ['blackscholes', 'bodytrack', 'average']
    benchmarks = [
        'blackscholes', 'bodytrack', 'canneal', 'dedup', 'ferret',
        'fluidanimate', 'vips', 'x264'
    ]
    speedup_xlabels = [
        'blackscholes', 'bodytrack', 'canneal', 'dedup', 'ferret',
        'fluidanimate', 'vips', 'x264', 'gmean'
    ]
    energy_xlabels = [
        'blackscholes', 'bodytrack', 'canneal', 'dedup', 'ferret',
        'fluidanimate', 'vips', 'x264', 'average'
    ]
    energy_components = ['Static Energy', 'Dynamic Energy']

    runtimes = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    norm_runtimes = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)

    energy = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    energy_breakdown = np.zeros(
        (int(len(energy_components)), int(len(schemes) * len(benchmarks))),
        dtype=np.float)
    norm_energy_breakdown = np.zeros(
        (int(len(energy_components)), int(len(schemes) * len(benchmarks))),
        dtype=np.float)

    energy_delay_product = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)

    paper_norm_runtimes = np.zeros(
        (int(len(paper_schemes)), int(len(benchmarks))), dtype=np.float)
    speedup = np.zeros(
        (int(len(paper_schemes)), int(len(speedup_xlabels))), dtype=np.float)
    paper_norm_energy_breakdown = np.zeros(
        (int(len(paper_schemes) * len(energy_xlabels)),
         int(len(energy_components))),
        dtype=np.float)

    average_norm_total_energy = np.zeros(
        int(len(paper_schemes)), dtype=np.float)
    average_norm_dynamic_energy = np.zeros(
        int(len(paper_schemes)), dtype=np.float)
    average_norm_static_energy = np.zeros(
        int(len(paper_schemes)), dtype=np.float)

    router_config_file = "ext/dsent/configs/booksim_router.cfg"
    link_config_file = "ext/dsent/configs/booksim_link.cfg"
    for s, scheme in enumerate(schemes):
        for b, benchmark in enumerate(benchmarks):

            # runtime
            filename = 'm5out/restore_' + scheme + '/' + benchmark + '32_8x8mesh_simsmall/stats.txt'

            statsfile = open(filename)
            for l, line in enumerate(statsfile):
                if 'sim_seconds' in line:
                    line = line.split()
                    runtime = float(line[1])
                    runtimes[s][b] = runtime
                    break

            norm_runtimes[s][b] = runtimes[s][b] / runtimes[0][b]

            # energy
            sim_directory = "m5out/restore_" + scheme + "/" + benchmark + "32_8x8mesh_simsmall"

            _, _, dynamic_energy, static_energy = power_model.getPowerAndEnergy(
                sim_directory, router_config_file, link_config_file)

            energy[s][b] = dynamic_energy + static_energy
            energy_breakdown[0][b * len(schemes) + s] = static_energy
            energy_breakdown[1][b * len(schemes) + s] = dynamic_energy
            baseline_total_energy = energy_breakdown[0][b * len(
                schemes)] + energy_breakdown[1][b * len(schemes)]

            norm_energy_breakdown[0][
                b * len(schemes) + s] = static_energy / baseline_total_energy
            norm_energy_breakdown[1][
                b * len(schemes) + s] = dynamic_energy / baseline_total_energy

    energy_delay_product = energy * runtimes
    paper_norm_runtimes[[0, 1], :] = norm_runtimes[[0, 1], :]
    paper_norm_runtimes[2:, :] = norm_runtimes[3:, :]

    del_cols = []
    for b, benchmark in enumerate(benchmarks):
        if energy_delay_product[1][b] > energy_delay_product[2][b]:
            paper_norm_runtimes[1][b] = norm_runtimes[2][b]
            del_cols.append(b * len(schemes) + 1)
        else:
            del_cols.append(b * len(schemes) + 2)

    for s, scheme in enumerate(paper_schemes):
        speedup[s, 0:-1] = 1 / paper_norm_runtimes[s, :]
        speedup[s][-1] = stats.mstats.gmean(speedup[s][0:-1])

    data = [list(i) for i in zip(*norm_energy_breakdown)]
    data = np.array(data, dtype=np.float)
    data = np.transpose(data)
    data = np.delete(data, del_cols, 1)
    paper_norm_energy_breakdown[:-len(paper_schemes), :] = np.transpose(data)
    for s, scheme in enumerate(paper_schemes):
        for e, energy in enumerate(energy_components):
            energy = [
                paper_norm_energy_breakdown[b * len(paper_schemes) + s][e]
                for b in range(len(benchmarks))
            ]
            mean_energy = np.mean(energy)
            paper_norm_energy_breakdown[len(benchmarks) * len(paper_schemes)
                                        + s][e] = mean_energy
            average_norm_total_energy[s] += mean_energy

        average_norm_static_energy[s] = paper_norm_energy_breakdown[
            len(benchmarks) * len(paper_schemes) + s][0]
        average_norm_dynamic_energy[s] = paper_norm_energy_breakdown[
            len(benchmarks) * len(paper_schemes) + s][1]
    average_norm_static_energy = average_norm_static_energy / average_norm_static_energy[
        0]
    average_norm_dynamic_energy = average_norm_dynamic_energy / average_norm_dynamic_energy[
        1]
    average_norm_total_energy_over_rp = average_norm_total_energy[
        1:] / average_norm_total_energy[1]
    average_norm_static_energy_over_rp = average_norm_static_energy[
        1:] / average_norm_static_energy[1]
    average_norm_dynamic_energy_over_rp = average_norm_dynamic_energy[
        1:] / average_norm_dynamic_energy[1]
    print("runtime speedup over Baseline: ", speedup[:, -1])
    print("runtime speedup over RP: ", (speedup[1:][-1] / speedup[1][-1]))
    print("average normalized total energy over Baseline: ",
          average_norm_total_energy)
    print("average normalized dynamic energy over Baseline: ",
          average_norm_dynamic_energy)
    print("average normalized static energy over Baseline: ",
          average_norm_static_energy)
    print("average normalized total energy over RP: ",
          average_norm_total_energy_over_rp)
    print("average normalized dynamic energy over RP: ",
          average_norm_dynamic_energy_over_rp)
    print("average normalized static energy over RP: ",
          average_norm_static_energy_over_rp)

    plt.rc('legend', fontsize=12)

    # runtime speedup
    colors = ['#f1eef6', '#d0d1e6', '#a6bddb', '#74a9cf', '#2b8cbe', '#045a8d']
    data = [list(i) for i in zip(*speedup)]
    figname = 'parsec_speedup.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(12, 4), fontsize=14)
    ax = fig.gca()
    hdls = barchart.draw(
        ax,
        data,
        group_names=speedup_xlabels,
        entry_names=paper_schemes,
        colors=colors,
        breakdown=False,
        legendloc='upper center',
        legendncol=len(paper_schemes))
    fig.autofmt_xdate()
    ax.yaxis.grid(True, linestyle='--')
    ax.set_ylabel('Runtime Speedup')
    ax.set_xlabel('Benchmarks')
    ax.legend(
        hdls,
        paper_schemes,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.18),
        ncol=len(paper_schemes),
        frameon=False,
        handletextpad=0.1,
        columnspacing=0.5)
    fmt.resize_ax_box(ax, hratio=0.8)
    pdf.plot_teardown(pdfpage, fig)

    # energy
    group_names = []
    xticks = []
    for b, benchmark in enumerate(energy_xlabels):
        for s, scheme in enumerate(paper_schemes):
            group_names.append(scheme)
            xticks.append(b * (len(paper_schemes) + 1) + s)

    colors = ['#0570b0', '#fee0d2']
    figname = "parsec_energy_breakdown.pdf"
    pdfpage, fig = pdf.plot_setup(figname, figsize=(12, 4), fontsize=14)
    ax = fig.gca()
    hdls = barchart.draw(
        ax,
        paper_norm_energy_breakdown,
        group_names=group_names,
        entry_names=energy_components,
        breakdown=True,
        xticks=xticks,
        colors=colors,
        legendloc='upper center',
        legendncol=2,
        xticklabelfontsize=11,
        xticklabelrotation=90)
    ax.set_ylabel('Normalized Energy')
    ax.set_xlabel('Benchmarks')
    ax.xaxis.set_label_coords(0.5, -0.55)
    ax.yaxis.grid(True, linestyle='--')
    ax.legend(
        hdls,
        energy_components,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.2),
        ncol=2,
        frameon=False,
        handletextpad=0.1,
        columnspacing=1)
    fmt.resize_ax_box(ax, hratio=0.8)
    ly = len(energy_xlabels)
    scale = 1. / ly
    ypos = -.5
    pos = 0
    for pos in xrange(ly + 1):
        lxpos = (pos + 0.5) * scale
        if pos < ly:
            ax.text(
                lxpos,
                ypos,
                energy_xlabels[pos],
                ha='center',
                transform=ax.transAxes,
                fontsize=12)
            add_line(ax, pos * scale, ypos)
    add_line(ax, 1, ypos)
    fig.subplots_adjust(bottom=0.38)
    pdf.plot_teardown(pdfpage, fig)


if __name__ == '__main__':
    main()
