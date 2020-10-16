#!/usr/bin/python

import sys
import os
import numpy as np
import json
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
        'baseline', 'rpc', 'rpa', 'nord', 'flov', 'opt_flov'
    ]
    paper_schemes = [
        'Baseline', 'RP', 'NoRD', 'FLOV', 'FLOV+'
    ]
    #benchmarks = ['blackscholes', 'bodytrack']
    #gmean_xlabels = ['blackscholes', 'bodytrack', 'gmean']
    #avg_xlabels = ['blackscholes', 'bodytrack', 'average']
    benchmarks = [
        'blackscholes', 'bodytrack', 'canneal', 'dedup', 'ferret',
        'fluidanimate', 'vips', 'x264'
    ]
    gmean_xlabels = [
        'blackscholes', 'bodytrack', 'canneal', 'dedup', 'ferret',
        'fluidanimate', 'vips', 'x264', 'gmean'
    ]
    avg_xlabels = [
        'blackscholes', 'bodytrack', 'canneal', 'dedup', 'ferret',
        'fluidanimate', 'vips', 'x264', 'average'
    ]
    energy_components = ['Static Energy', 'Dynamic Energy']
    power_components = ['Static Power', 'Dynamic Power']

    runtimes = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    norm_runtimes = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    latency = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    hops = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    flov_hops = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)

    energy = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    dynamic_energies = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    static_energies = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    energy_breakdown = np.zeros(
        (int(len(energy_components)), int(len(schemes) * len(benchmarks))),
        dtype=np.float)
    norm_energy_breakdown = np.zeros(
        (int(len(energy_components)), int(len(schemes) * len(benchmarks))),
        dtype=np.float)

    power = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    dynamic_powers = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    static_powers = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    power_breakdown = np.zeros(
        (int(len(energy_components)), int(len(schemes) * len(benchmarks))),
        dtype=np.float)
    norm_power_breakdown = np.zeros(
        (int(len(energy_components)), int(len(schemes) * len(benchmarks))),
        dtype=np.float)

    energy_delay_product = np.zeros(
        (int(len(schemes)), int(len(benchmarks))), dtype=np.float)
    paper_energy_delay_product = np.zeros(
        (int(len(paper_schemes)), int(len(benchmarks))), dtype=np.float)
    paper_norm_edp = np.zeros(
        (int(len(paper_schemes)), int(len(gmean_xlabels))), dtype=np.float)

    paper_norm_runtimes = np.zeros(
        (int(len(paper_schemes)), int(len(benchmarks))), dtype=np.float)
    speedup = np.zeros(
        (int(len(paper_schemes)), int(len(gmean_xlabels))), dtype=np.float)
    paper_latency = np.zeros(
        (int(len(paper_schemes)), int(len(avg_xlabels))), dtype=np.float)
    paper_hops = np.zeros(
        (int(len(paper_schemes)), int(len(avg_xlabels))), dtype=np.float)
    paper_flov_hops = np.zeros(
        (int(len(paper_schemes)), int(len(avg_xlabels))), dtype=np.float)
    paper_norm_energy_breakdown = np.zeros(
        (int(len(paper_schemes) * len(avg_xlabels)),
         int(len(energy_components))),
        dtype=np.float)
    paper_norm_power_breakdown = np.zeros(
        (int(len(paper_schemes) * len(avg_xlabels)),
         int(len(energy_components))),
        dtype=np.float)
    paper_norm_static_energy = np.zeros(
            (int(len(paper_schemes)), int(len(avg_xlabels))), dtype=np.float)
    paper_norm_dynamic_energy = np.zeros(
            (int(len(paper_schemes)), int(len(avg_xlabels))), dtype=np.float)
    paper_norm_static_power = np.zeros(
            (int(len(paper_schemes)), int(len(avg_xlabels))), dtype=np.float)
    paper_norm_dynamic_power = np.zeros(
            (int(len(paper_schemes)), int(len(avg_xlabels))), dtype=np.float)

    average_norm_total_energy = np.zeros(
        int(len(paper_schemes)), dtype=np.float)
    average_norm_dynamic_energy = np.zeros(
        int(len(paper_schemes)), dtype=np.float)
    average_norm_static_energy = np.zeros(
        int(len(paper_schemes)), dtype=np.float)

    average_norm_total_power = np.zeros(
        int(len(paper_schemes)), dtype=np.float)
    average_norm_dynamic_power = np.zeros(
        int(len(paper_schemes)), dtype=np.float)
    average_norm_static_power = np.zeros(
        int(len(paper_schemes)), dtype=np.float)

    if os.path.exists('results_8c.npz'):
        results = np.load('results_8c.npz')

        # Raw resutls
        runtimes = results['runtimes']
        norm_runtimes = results['norm_runtimes']
        energy = results['energy']
        energy_breakdown = results['energy_breakdown']
        dynamic_energies = results['dynamic_energies']
        static_energies = results['static_energies']
        norm_energy_breakdown = results['norm_energy_breakdown']
        power = results['power']
        power_breakdown = results['power_breakdown']
        dynamic_powers = results['dynamic_powers']
        static_powers = results['static_powers']
        norm_power_breakdown = results['norm_power_breakdown']
        energy_delay_product = results['energy_delay_product']
        latency = results['latency']
        hops = results['hops']
        flov_hops = results['flov_hops']

        # paper resutls
        paper_energy_delay_product = results['paper_energy_delay_product']
        paper_norm_edp = results['paper_norm_edp']
        paper_norm_runtimes = results['paper_norm_runtimes']
        speedup = results['speedup']
        paper_norm_energy_breakdown = results['paper_norm_energy_breakdown']
        paper_norm_static_energy = results['paper_norm_static_energy']
        paper_norm_dynamic_energy = results['paper_norm_dynamic_energy']
        paper_norm_power_breakdown = results['paper_norm_power_breakdown']
        paper_norm_static_power = results['paper_norm_static_power']
        paper_norm_dynamic_power = results['paper_norm_dynamic_power']
        paper_latency = results['paper_latency']
        paper_hops = results['paper_hops']
        paper_flov_hops = results['paper_flov_hops']

        # others
        average_norm_total_energy = results['average_norm_total_energy']
        average_norm_dynamic_energy = results['average_norm_dynamic_energy']
        average_norm_static_energy = results['average_norm_static_energy']
        average_norm_total_power = results['average_norm_total_power']
        average_norm_dynamic_power = results['average_norm_dynamic_power']
        average_norm_static_power = results['average_norm_static_power']
        average_norm_total_energy_over_rp = results['average_norm_total_energy_over_rp']
        average_norm_dynamic_energy_over_rp = results['average_norm_dynamic_energy_over_rp']
        average_norm_static_energy_over_rp = results['average_norm_static_energy_over_rp']
        average_norm_total_power_over_rp = results['average_norm_total_power_over_rp']
        average_norm_dynamic_power_over_rp = results['average_norm_dynamic_power_over_rp']
        average_norm_static_power_over_rp = results['average_norm_static_power_over_rp']
        average_norm_total_energy_over_nord = results['average_norm_total_energy_over_nord']
        average_norm_dynamic_energy_over_nord = results['average_norm_dynamic_energy_over_nord']
        average_norm_static_energy_over_nord = results['average_norm_static_energy_over_nord']
        average_norm_total_power_over_nord = results['average_norm_total_power_over_nord']
        average_norm_dynamic_power_over_nord = results['average_norm_dynamic_power_over_nord']
        average_norm_static_power_over_nord = results['average_norm_static_power_over_nord']

    else:
        router_config_file = "ext/dsent/configs/booksim_router.cfg"
        link_config_file = "ext/dsent/configs/booksim_link.cfg"
        for s, scheme in enumerate(schemes):
            for b, benchmark in enumerate(benchmarks):

                # runtime
                filename = 'm5out-8c-4x4mesh/restore_' + scheme + '/' + benchmark + '8_4x4mesh_simsmall/stats.txt'

                statsfile = open(filename)
                for l, line in enumerate(statsfile):
                    if 'sim_seconds' in line:
                        line = line.split()
                        runtime = float(line[1])
                        runtimes[s][b] = runtime
                        break

                norm_runtimes[s][b] = runtimes[s][b] / runtimes[0][b]

                # energy
                sim_directory = "m5out-8c-4x4mesh/restore_" + scheme + "/" + benchmark + "8_4x4mesh_simsmall"

                dynamic_power, static_power, dynamic_energy, static_energy = power_model.getPowerAndEnergy(
                    sim_directory, router_config_file, link_config_file)
                if static_energy < 0:
                    print('Negative power: {}'.format(sim_directory))

                energy[s][b] = dynamic_energy + static_energy
                dynamic_energies[s][b] = dynamic_energy
                static_energies[s][b] = static_energy
                energy_breakdown[0][b * len(schemes) + s] = static_energy
                energy_breakdown[1][b * len(schemes) + s] = dynamic_energy
                baseline_total_energy = energy_breakdown[0][b * len(
                    schemes)] + energy_breakdown[1][b * len(schemes)]

                norm_energy_breakdown[0][
                    b * len(schemes) + s] = static_energy / baseline_total_energy
                norm_energy_breakdown[1][
                    b * len(schemes) + s] = dynamic_energy / baseline_total_energy

                power[s][b] = dynamic_power + static_power
                dynamic_powers[s][b] = dynamic_power
                static_powers[s][b] = static_power
                power_breakdown[0][b * len(schemes) + s] = static_power
                power_breakdown[1][b * len(schemes) + s] = dynamic_power
                baseline_total_power = power_breakdown[0][b * len(
                    schemes)] + power_breakdown[1][b * len(schemes)]

                norm_power_breakdown[0][
                    b * len(schemes) + s] = static_power / baseline_total_power
                norm_power_breakdown[1][
                    b * len(schemes) + s] = dynamic_power / baseline_total_power

                # latency and hop count
                booksim_stats_file = 'm5out-8c-4x4mesh/restore_{}/{}8_4x4mesh_simsmall/booksimstats.json'.format(scheme, benchmark)
                with open(booksim_stats_file) as jsonfile:
                    booksim_stats = json.load(jsonfile)
                    latency[s][b] = booksim_stats['packet-latency']['average']
                    hops[s][b] = booksim_stats['hops-average']
                    if scheme == 'flov' or scheme == 'opt_flov':
                        flov_hops[s][b] = booksim_stats['flov-hops-average']

        energy_delay_product = energy * runtimes
        paper_norm_runtimes[[0, 1], :] = norm_runtimes[[0, 1], :]
        paper_norm_runtimes[2:, :] = norm_runtimes[3:, :]
        paper_energy_delay_product[[0, 1], :] = energy_delay_product[[0, 1], :]
        paper_energy_delay_product[2:, :] = energy_delay_product[3:, :]
        paper_latency[[0, 1], :-1] = latency[[0, 1], :]
        paper_latency[2:, :-1] = latency[3:, :]
        paper_hops[[0, 1], :-1] = hops[[0, 1], :]
        paper_hops[2:, :-1] = hops[3:, :]
        paper_flov_hops[[0, 1], :-1] = flov_hops[[0, 1], :]
        paper_flov_hops[2:, :-1] = flov_hops[3:, :]

        del_cols = []
        for b, benchmark in enumerate(benchmarks):
            if energy_delay_product[1][b] > energy_delay_product[2][b]:
                paper_norm_runtimes[1][b] = norm_runtimes[2][b]
                paper_energy_delay_product[1][b] = energy_delay_product[2][b]
                paper_latency[1][b] = latency[2][b]
                paper_hops[1][b] = hops[2][b]
                paper_flov_hops[1][b] = flov_hops[2][b]
                del_cols.append(b * len(schemes) + 1)
            else:
                del_cols.append(b * len(schemes) + 2)

        # speedup
        for s, scheme in enumerate(paper_schemes):
            speedup[s, 0:-1] = 1 / paper_norm_runtimes[s, :]
            speedup[s][-1] = stats.mstats.gmean(speedup[s][0:-1])

        # energy-delay product
        for s, scheme in enumerate(paper_schemes):
            for b, benchmark in enumerate(benchmarks):
                paper_norm_edp[s][b] = paper_energy_delay_product[s][b] / paper_energy_delay_product[0][b]
            paper_norm_edp[s][-1] = stats.mstats.gmean(paper_norm_edp[s][0:-1])

        # latency and hops
        for s, scheme in enumerate(paper_schemes):
            paper_latency[s][-1] = np.mean(paper_latency[s][0:-1])
            paper_hops[s][-1] = np.mean(paper_hops[s][0:-1])
            paper_flov_hops[s][-1] = np.mean(paper_flov_hops[s][0:-1])

        # energy
        data = [list(i) for i in zip(*norm_energy_breakdown)]
        data = np.array(data, dtype=np.float)
        data = np.transpose(data)
        data = np.delete(data, del_cols, 1)
        paper_norm_energy_breakdown[:-len(paper_schemes), :] = np.transpose(data)
        paper_static_energy = np.zeros(
                (int(len(paper_schemes)), int(len(benchmarks))), dtype=np.float)
        paper_dynamic_energy = np.zeros(
                (int(len(paper_schemes)), int(len(benchmarks))), dtype=np.float)
        for s, scheme in enumerate(paper_schemes):
            for e, energy_compoent in enumerate(energy_components):
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

            for b, benchmark in enumerate(benchmarks):
                paper_static_energy[s][b] = paper_norm_energy_breakdown[b * len(paper_schemes) + s][0]
                paper_dynamic_energy[s][b] = paper_norm_energy_breakdown[b * len(paper_schemes) + s][1]
                paper_norm_static_energy[s][b] = paper_static_energy[s][b] / paper_static_energy[0][b]
                paper_norm_dynamic_energy[s][b] = paper_dynamic_energy[s][b] / paper_dynamic_energy[0][b]
            paper_norm_static_energy[s][-1] = stats.mstats.gmean(paper_norm_static_energy[s][0:-1])
            paper_norm_dynamic_energy[s][-1] = stats.mstats.gmean(paper_norm_dynamic_energy[s][0:-1])

        average_norm_static_energy = average_norm_static_energy / average_norm_static_energy[
            0]
        average_norm_dynamic_energy = average_norm_dynamic_energy / average_norm_dynamic_energy[
            0]
        average_norm_total_energy_over_rp = average_norm_total_energy[
            1:] / average_norm_total_energy[1]
        average_norm_static_energy_over_rp = average_norm_static_energy[
            1:] / average_norm_static_energy[1]
        average_norm_dynamic_energy_over_rp = average_norm_dynamic_energy[
            1:] / average_norm_dynamic_energy[1]
        average_norm_total_energy_over_nord = average_norm_total_energy[
            2:] / average_norm_total_energy[2]
        average_norm_static_energy_over_nord = average_norm_static_energy[
            2:] / average_norm_static_energy[2]
        average_norm_dynamic_energy_over_nord = average_norm_dynamic_energy[
            2:] / average_norm_dynamic_energy[2]

        # power
        data = [list(i) for i in zip(*norm_power_breakdown)]
        data = np.array(data, dtype=np.float)
        data = np.transpose(data)
        data = np.delete(data, del_cols, 1)
        paper_norm_power_breakdown[:-len(paper_schemes), :] = np.transpose(data)
        paper_static_power = np.zeros(
                (int(len(paper_schemes)), int(len(benchmarks))), dtype=np.float)
        paper_dynamic_power = np.zeros(
                (int(len(paper_schemes)), int(len(benchmarks))), dtype=np.float)
        for s, scheme in enumerate(paper_schemes):
            for p, component in enumerate(power_components):
                power = [
                    paper_norm_power_breakdown[b * len(paper_schemes) + s][p]
                    for b in range(len(benchmarks))
                ]
                mean_power = np.mean(power)
                paper_norm_power_breakdown[len(benchmarks) * len(paper_schemes)
                                            + s][p] = mean_power
                average_norm_total_power[s] += mean_power

            average_norm_static_power[s] = paper_norm_power_breakdown[
                len(benchmarks) * len(paper_schemes) + s][0]
            average_norm_dynamic_power[s] = paper_norm_power_breakdown[
                len(benchmarks) * len(paper_schemes) + s][1]

            for b, benchmark in enumerate(benchmarks):
                paper_static_power[s][b] = paper_norm_power_breakdown[b * len(paper_schemes) + s][0]
                paper_dynamic_power[s][b] = paper_norm_power_breakdown[b * len(paper_schemes) + s][1]
                paper_norm_static_power[s][b] = paper_static_power[s][b] / paper_static_power[0][b]
                paper_norm_dynamic_power[s][b] = paper_dynamic_power[s][b] / paper_dynamic_power[0][b]
            paper_norm_static_power[s][-1] = stats.mstats.gmean(paper_norm_static_power[s][0:-1])
            paper_norm_dynamic_power[s][-1] = stats.mstats.gmean(paper_norm_dynamic_power[s][0:-1])

        average_norm_static_power = average_norm_static_power / average_norm_static_power[
            0]
        average_norm_dynamic_power = average_norm_dynamic_power / average_norm_dynamic_power[
            0]
        average_norm_total_power_over_rp = average_norm_total_power[
            1:] / average_norm_total_power[1]
        average_norm_static_power_over_rp = average_norm_static_power[
            1:] / average_norm_static_power[1]
        average_norm_dynamic_power_over_rp = average_norm_dynamic_power[
            1:] / average_norm_dynamic_power[1]
        average_norm_total_power_over_nord = average_norm_total_power[
            2:] / average_norm_total_power[2]
        average_norm_static_power_over_nord = average_norm_static_power[
            2:] / average_norm_static_power[2]
        average_norm_dynamic_power_over_nord = average_norm_dynamic_power[
            2:] / average_norm_dynamic_power[2]

        np.savez('results_8c.npz',
                # raw data
                runtimes=runtimes, norm_runtimes=norm_runtimes,
                energy=energy, energy_breakdown=energy_breakdown,
                dynamic_energies=dynamic_energies,
                static_energies=static_energies,
                norm_energy_breakdown=norm_energy_breakdown,
                power=power, power_breakdown=power_breakdown,
                dynamic_powers=dynamic_powers,
                static_powers=static_powers,
                norm_power_breakdown=norm_power_breakdown,
                energy_delay_product=energy_delay_product,
                paper_energy_delay_product=paper_energy_delay_product,
                paper_norm_edp=paper_norm_edp,
                latency=latency,
                hops=hops,
                flov_hops=flov_hops,
                # paper results
                paper_norm_runtimes=paper_norm_runtimes, speedup=speedup,
                paper_norm_energy_breakdown=paper_norm_energy_breakdown,
                paper_norm_static_energy=paper_norm_static_energy,
                paper_norm_dynamic_energy=paper_norm_dynamic_energy,
                paper_norm_power_breakdown=paper_norm_power_breakdown,
                paper_norm_static_power=paper_norm_static_power,
                paper_norm_dynamic_power=paper_norm_dynamic_power,
                paper_latency=paper_latency,
                paper_hops=paper_hops,
                paper_flov_hops=paper_flov_hops,
                # over baseline
                average_norm_total_energy=average_norm_total_energy,
                average_norm_dynamic_energy=average_norm_dynamic_energy,
                average_norm_static_energy=average_norm_static_energy,
                average_norm_total_power=average_norm_total_power,
                average_norm_dynamic_power=average_norm_dynamic_power,
                average_norm_static_power=average_norm_static_power,
                # over RP
                average_norm_total_energy_over_rp=average_norm_total_energy_over_rp,
                average_norm_dynamic_energy_over_rp=average_norm_dynamic_energy_over_rp,
                average_norm_static_energy_over_rp=average_norm_static_energy_over_rp,
                average_norm_total_power_over_rp=average_norm_total_power_over_rp,
                average_norm_dynamic_power_over_rp=average_norm_dynamic_power_over_rp,
                average_norm_static_power_over_rp=average_norm_static_power_over_rp,
                # over nord
                average_norm_total_energy_over_nord=average_norm_total_energy_over_nord,
                average_norm_dynamic_energy_over_nord=average_norm_dynamic_energy_over_nord,
                average_norm_static_energy_over_nord=average_norm_static_energy_over_nord,
                average_norm_total_power_over_nord=average_norm_total_power_over_nord,
                average_norm_dynamic_power_over_nord=average_norm_dynamic_power_over_nord,
                average_norm_static_power_over_nord=average_norm_static_power_over_nord)

    print("runtime speedup over Baseline: ", speedup[:, -1])
    print("runtime speedup over RP: ", (speedup[1:,-1] / speedup[1][-1]))
    print("runtime speedup over NoRD: ", (speedup[2:,-1] / speedup[2][-1]))
    print("average normalized total energy over Baseline: ",
          average_norm_total_energy)
    print("average normalized dynamic energy over Baseline: ",
          average_norm_dynamic_energy)
    print("average normalized static energy over Baseline: ",
          average_norm_static_energy)
    print("average normalized total power over Baseline: ",
          average_norm_total_power)
    print("average normalized dynamic power over Baseline: ",
          average_norm_dynamic_power)
    print("average normalized static power over Baseline: ",
          average_norm_static_power)
    print("average normalized total energy over RP: ",
          average_norm_total_energy_over_rp)
    print("average normalized dynamic energy over RP: ",
          average_norm_dynamic_energy_over_rp)
    print("average normalized static energy over RP: ",
          average_norm_static_energy_over_rp)
    print("average normalized total power over RP: ",
          average_norm_total_power_over_rp)
    print("average normalized dynamic poewr over RP: ",
          average_norm_dynamic_power_over_rp)
    print("average normalized static power over RP: ",
          average_norm_static_power_over_rp)
    print("average normalized total energy over NoRD: ",
          average_norm_total_energy_over_nord)
    print("average normalized dynamic energy over NoRD: ",
          average_norm_dynamic_energy_over_nord)
    print("average normalized static energy over NoRD: ",
          average_norm_static_energy_over_nord)
    print("average normalized total power over NoRD: ",
          average_norm_total_power_over_nord)
    print("average normalized dynamic poewr over NoRD: ",
          average_norm_dynamic_power_over_nord)
    print("average normalized static power over NoRD: ",
          average_norm_static_power_over_nord)

    # write to a file
    with open('results_8c.csv', mode='w') as result_file:
        # write runtime
        result_file.write('Raw runtime\n')
        for s, scheme in enumerate(schemes):
            result_file.write(',{}'.format(scheme))
        result_file.write('\n')
        for b, benchmark in enumerate(benchmarks):
            result_file.write(benchmark)
            for s, scheme in enumerate(schemes):
                result_file.write(',{}'.format(runtimes[s][b]))
            result_file.write('\n')
        result_file.write('\n')

        # write normalized runtime
        result_file.write('Normalized runtime\n')
        for s, scheme in enumerate(paper_schemes):
            result_file.write(',{}'.format(scheme))
        result_file.write('\n')
        for b, benchmark in enumerate(benchmarks):
            result_file.write(benchmark)
            for s, scheme in enumerate(paper_schemes):
                result_file.write(',{}'.format(paper_norm_runtimes[s][b]))
            result_file.write('\n')
        result_file.write('\n')

        # write speedup
        result_file.write('Speedup\n')
        for s, scheme in enumerate(paper_schemes):
            result_file.write(',{}'.format(scheme))
        result_file.write('\n')
        for b, benchmark in enumerate(benchmarks):
            result_file.write(benchmark)
            for s, scheme in enumerate(paper_schemes):
                result_file.write(',{}'.format(speedup[s][b]))
            result_file.write('\n')
        result_file.write('\n')

        # write energy
        result_file.write('Raw energy\n')
        result_file.write(',,Static Energy,Dynamic Energy\n')
        for b, benchmark in enumerate(benchmarks):
            result_file.write(benchmark)
            for s, scheme in enumerate(schemes):
                result_file.write(',{},{},{}\n'.format(scheme,
                    energy_breakdown[0][b * len(schemes) + s],
                    energy_breakdown[1][b * len(schemes) + s]))
        result_file.write('\n')

        # write nromalized energy
        result_file.write('Normalized energy\n')
        result_file.write(',,Static Energy,Dynamic Energy\n')
        for b, benchmark in enumerate(benchmarks):
            result_file.write(benchmark)
            for s, scheme in enumerate(paper_schemes):
                result_file.write(',{},{},{}\n'.format(scheme,
                    paper_norm_energy_breakdown[b * len(paper_schemes) + s][0],
                    paper_norm_energy_breakdown[b * len(paper_schemes) + s][1]))
        result_file.write('\n')

        # write power
        result_file.write('Raw power\n')
        result_file.write(',,Static Power,Dynamic Power\n')
        for b, benchmark in enumerate(benchmarks):
            result_file.write(benchmark)
            for s, scheme in enumerate(schemes):
                result_file.write(',{},{},{}\n'.format(scheme,
                    static_powers[s][b], dynamic_powers[s][b]))
                    #power_breakdown[0][b * len(schemes) + s],
                    #power_breakdown[1][b * len(schemes) + s]))
        result_file.write('\n')

        # write nromalized power
        result_file.write('Normalized power\n')
        result_file.write(',,Static Power,Dynamic Power\n')
        for b, benchmark in enumerate(benchmarks):
            result_file.write(benchmark)
            for s, scheme in enumerate(paper_schemes):
                result_file.write(',{},{},{}\n'.format(scheme,
                    paper_norm_power_breakdown[b * len(paper_schemes) + s][0],
                    paper_norm_power_breakdown[b * len(paper_schemes) + s][1]))
        result_file.write('\n')

        result_file.close()

    plt.rc('legend', fontsize=12)

    # runtime speedup
    colors = ['#f1eef6', '#d0d1e6', '#a6bddb', '#74a9cf', '#2b8cbe', '#045a8d']
    colors = ['#eff3ff','#bdd7e7','#6baed6','#3182bd','#08519c']
    data = [list(i) for i in zip(*speedup)]
    figname = 'parsec_speedup.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    hdls = barchart.draw(
        ax,
        data,
        group_names=gmean_xlabels,
        entry_names=paper_schemes,
        colors=colors,
        breakdown=False,
        legendloc='upper center',
        legendncol=len(paper_schemes))
    fig.autofmt_xdate()
    ax.yaxis.grid(True, linestyle='--')
    ax.set_ylabel('Runtime Speedup')
    #ax.set_xlabel('Benchmarks')
    ax.legend(
        hdls,
        paper_schemes,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.18),
        ncol=len(paper_schemes),
        frameon=False,
        handletextpad=0.5,
        columnspacing=1)
    fmt.resize_ax_box(ax, hratio=0.8)
    pdf.plot_teardown(pdfpage, fig)

    # latency
    data = [list(i) for i in zip(*paper_latency)]
    figname = 'parsec_latency.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    hdls = barchart.draw(
        ax,
        data,
        group_names=gmean_xlabels,
        entry_names=paper_schemes,
        colors=colors,
        breakdown=False,
        legendloc='upper center',
        legendncol=len(paper_schemes))
    fig.autofmt_xdate()
    ax.yaxis.grid(True, linestyle='--')
    ax.set_ylabel('Average Packet Latency (cycles)')
    #ax.set_xlabel('Benchmarks')
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

    # hops
    data = paper_hops + paper_flov_hops
    data = [list(i) for i in zip(*data)]
    figname = 'parsec_hops.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    hdls = barchart.draw(
        ax,
        data,
        group_names=gmean_xlabels,
        entry_names=paper_schemes,
        colors=colors,
        breakdown=False,
        legendloc='upper center',
        legendncol=len(paper_schemes))
    fig.autofmt_xdate()
    ax.yaxis.grid(True, linestyle='--')
    ax.set_ylabel('Average Packet Hops')
    #ax.set_xlabel('Benchmarks')
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

    # energy-delay product
    data = [list(i) for i in zip(*paper_norm_edp)]
    figname = 'parsec_edp.pdf'
    pdfpage, fig = pdf.plot_setup(figname, figsize=(8, 4), fontsize=14)
    ax = fig.gca()
    hdls = barchart.draw(
        ax,
        data,
        group_names=gmean_xlabels,
        entry_names=paper_schemes,
        colors=colors,
        breakdown=False,
        legendloc='upper center',
        legendncol=len(paper_schemes))
    fig.autofmt_xdate()
    ax.yaxis.grid(True, linestyle='--')
    ax.set_ylabel('Normalized Energy-Delay Product')
    #ax.set_xlabel('Benchmarks')
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
    for b, benchmark in enumerate(avg_xlabels):
        for s, scheme in enumerate(paper_schemes):
            group_names.append(scheme)
            xticks.append(b * (len(paper_schemes) + 1) + s)

    #colors = ['#0570b0', '#fee0d2']
    colors = ['#a63603','#fee6ce']
    figname = "parsec_energy_breakdown.pdf"
    pdfpage, fig = pdf.plot_setup(figname, figsize=(10, 4), fontsize=10)
    ax = fig.gca()
    ax2 = ax.twinx() # for normalized static energy, must put it here for the look
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
    #ax.set_xlabel('Benchmarks')
    ax.xaxis.set_label_coords(0.5, -0.55)
    ax.yaxis.grid(True, linestyle='--')
    ax.set_ylim([0, 2.5])
    fmt.resize_ax_box(ax, hratio=0.8)
    ly = len(avg_xlabels)
    scale = 1. / ly
    ypos = -.5
    pos = 0
    for pos in xrange(ly + 1):
        lxpos = (pos + 0.5) * scale
        if pos < ly:
            ax.text(
                lxpos,
                ypos,
                avg_xlabels[pos],
                ha='center',
                transform=ax.transAxes,
                fontsize=12)
            add_line(ax, pos * scale, ypos)
    add_line(ax, 1, ypos)
    # add static energy at secondary axis
    xs = []
    p = 0.0
    for g in range(len(avg_xlabels)):
        xs.append([])
        for pos in range(len(paper_schemes)):
            xs[g].append(p)
            p = p + 1
        p = p + 1
    data = [list(i) for i in zip(*paper_norm_static_energy)]
    data = np.array(data, dtype=np.float64)
    ax2.set_ylabel('Normalized Static Energy')
    ax2.set_ylim(0, 1.5)
    for i in range(len(avg_xlabels)):
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
        energy_components + ['Normalized Static Energy'],
        loc='upper center',
        bbox_to_anchor=(0.5, 1.2),
        ncol=len(energy_components) + 1,
        frameon=False,
        handletextpad=1,
        columnspacing=2)
    fig.subplots_adjust(bottom=0.38)
    pdf.plot_teardown(pdfpage, fig)

    figname = "parsec_power_breakdown.pdf"
    pdfpage, fig = pdf.plot_setup(figname, figsize=(10, 4), fontsize=14)
    ax = fig.gca()
    ax2 = ax.twinx() # for normalized static power, must put it here for the look
    hdls = barchart.draw(
        ax,
        paper_norm_power_breakdown,
        group_names=group_names,
        entry_names=energy_components,
        breakdown=True,
        xticks=xticks,
        colors=colors,
        legendloc='upper center',
        legendncol=2,
        xticklabelfontsize=11,
        xticklabelrotation=90)
    ax.set_ylabel('Normalized Power')
    #ax.set_xlabel('Benchmarks')
    ax.xaxis.set_label_coords(0.5, -0.55)
    ax.yaxis.grid(True, linestyle='--')
    ax.set_ylim([0, 1.2])
    fmt.resize_ax_box(ax, hratio=0.8)
    ly = len(avg_xlabels)
    scale = 1. / ly
    ypos = -.5
    pos = 0
    for pos in xrange(ly + 1):
        lxpos = (pos + 0.5) * scale
        if pos < ly:
            ax.text(
                lxpos,
                ypos,
                avg_xlabels[pos],
                ha='center',
                transform=ax.transAxes,
                fontsize=12)
            add_line(ax, pos * scale, ypos)
    add_line(ax, 1, ypos)
    # add static power at secondary axis
    xs = []
    p = 0.0
    for g in range(len(avg_xlabels)):
        xs.append([])
        for pos in range(len(paper_schemes)):
            xs[g].append(p)
            p = p + 1
        p = p + 1
    data = [list(i) for i in zip(*paper_norm_static_power)]
    data = np.array(data, dtype=np.float64)
    ax2.set_ylabel('Normalized Static Power')
    ax2.set_ylim(0, 1.2)
    for i in range(len(avg_xlabels)):
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
        power_components + ['Normalized Static Power'],
        loc='upper center',
        bbox_to_anchor=(0.5, 1.2),
        ncol=len(power_components) + 1,
        frameon=False,
        handletextpad=1,
        columnspacing=2)
    fig.subplots_adjust(bottom=0.38)
    pdf.plot_teardown(pdfpage, fig)


if __name__ == '__main__':
    main()
