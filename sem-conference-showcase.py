# AUTHOR(S):
# Mattia Lecci <mattia.lecci@dei.unipd.it>
# 
# University of Padova (UNIPD), Italy
# Information Engineering Department (DEI) 
# SIGNET Research Group @ http://signet.dei.unipd.it/
# 
# Date: December 2020

import sem
import os
import sys
import argparse
from collections import OrderedDict
import sem_utils
import numpy as np
from matplotlib import pyplot as plt

sys.stdout.flush()


def run_simulations(applicationType, appDataRate, socketType, mpduAggregationSize,
                    phyMode, simulationTime, numStas, allocationPeriod,
                    accessCbapIfAllocated, biDurationUs, onoffPeriodMean,
                    onoffPeriodStdev, numRuns):
    param_combination = OrderedDict({
        "applicationType": applicationType,
        "appDataRate": appDataRate,
        "socketType": socketType,
        "mpduAggregationSize": mpduAggregationSize,
        "phyMode": phyMode,
        "simulationTime": simulationTime,
        "numStas": numStas,
        "allocationPeriod": allocationPeriod,
        "accessCbapIfAllocated": accessCbapIfAllocated,
        "biDurationUs": biDurationUs,
        "onoffPeriodMean": onoffPeriodMean,
        "onoffPeriodStdev": onoffPeriodStdev,
        "RngRun": list(range(numRuns)),
    })

    campaign.run_missing_simulations(param_combination)
    broken_results = campaign.get_results_as_numpy_array(param_combination,
                                                         check_stderr,
                                                         numRuns)
    # remove_simulations(broken_results)
    #
    # print("Run simulations with param_combination " + str(param_combination))
    # campaign.run_missing_simulations(
    #     param_combination
    # )

    return param_combination


def remove_simulations(broken_results):
    print("Removing broken simulations")
    # TODO test map(campaign.db.delete_result, broken_results.flatten())
    for result in broken_results.flatten():
        if result:
            print("removing ", str(result))
            campaign.db.delete_result(result)
    # write updated database to disk
    campaign.db.write_to_disk()


def getAppDataRate(phy_mode, num_stas, norm_offered_traffic):
    phy_rate = sem_utils.MCS_PARAMS[phy_mode]['phy_rate']
    rate_per_sta = phy_rate / num_stas

    if type(norm_offered_traffic) is list:
        sta_rate = ["{:.0f}bps".format(r * rate_per_sta)
                    for r in norm_offered_traffic]
    elif type(norm_offered_traffic) is float:
        sta_rate = ["{:.0f}bps".format(norm_offered_traffic * rate_per_sta)]
    else:
        ValueError("Type of norm_offered_traffic not supported: ", type(norm_offered_traffic))

    return sta_rate


def check_stderr(result):
    if len(result['output']['stderr']) > 0:
        print('Invalid simulation: ', result['meta']['id'])
        return result
    else:
        return []


def plot_line_metric(campaign, parameter_space, result_parsing_function, runs, xx, hue_var, xlabel, ylabel, filename, xscale="linear", yscale="linear"):
    print("Plotting (line): ", result_parsing_function.__name__)
    metric = campaign.get_results_as_xarray(parameter_space,
                                            result_parsing_function,
                                            xlabel,
                                            runs)
    # average over numRuns and squeeze
    metric_mean = metric.reduce(np.mean, 'runs').squeeze()
    metric_ci95 = metric.reduce(np.std, 'runs').squeeze() * 1.96 / np.sqrt(runs)

    print(metric_mean.values)
    print(metric_ci95.values)

    fig = plt.figure()
    for val in metric_mean.coords[hue_var].values:
        plt.errorbar(xx, metric_mean.sel({hue_var: val}),
                     yerr=metric_ci95.sel({hue_var: val}),
                     label="{}={}".format(hue_var, val))
    plt.xscale(xscale)
    plt.yscale(yscale)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.legend()
    fig.savefig(os.path.join(img_dir, filename))


def plot_bar_metric(campaign, parameter_space, result_parsing_function, out_labels, runs, xlabel, ylabel, filename):
    print("Plotting (bar): ", result_parsing_function.__name__)
    metric = campaign.get_results_as_xarray(parameter_space,
                                             result_parsing_function,
                                             out_labels,
                                             runs)

    # extract group variable
    metric_dims = list(metric.squeeze().dims)
    metric_dims.remove("metrics")  # multiple output from parsing function mapped into "metrics"
    assert len(metric_dims) == 1, "There must only be one group_var, instead, metric_dims={}".format(metric_dims)
    group_var = metric_dims[0]

    # extract dict from xarray: each key corresponds to a group, each element is the array related to the x-axis
    metric_mean_dict = {}
    metric_ci95_dict = {}
    for x in metric.coords[group_var].values:
        metric_mean_dict["{}={}".format(group_var, x)] = metric.sel({group_var: x}).reduce(np.mean, 'runs').squeeze()
        metric_ci95_dict["{}={}".format(group_var, x)] = metric.sel({group_var: x}).reduce(np.std, 'runs').squeeze() * 1.96 / np.sqrt(runs)

    fig, ax = plt.subplots()
    sem_utils.bar_plot(ax, metric_mean_dict, metric_ci95_dict)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_xticks(range(len(out_labels)))
    ax.set_xticklabels(out_labels)
    fig.savefig(os.path.join(img_dir, filename))


def compute_avg_thr_mbps(pkts_df):
    if len(pkts_df) > 0:
        tstart = pkts_df['TxTimestamp_ns'].iloc[0] / 1e9
        tend = pkts_df['RxTimestamp_ns'].iloc[-1] / 1e9
        rx_mb = pkts_df['PktSize_B'].sum() * 8 / 1e6
        thr_mbps = rx_mb / (tend - tstart)
    else:
        thr_mbps = 0

    return thr_mbps


def compute_avg_delay_ms(pkts_df):
    if len(pkts_df) > 0:
        delay = (pkts_df['RxTimestamp_ns'] - pkts_df['TxTimestamp_ns']).mean() / 1e9 * 1e3  # [ms]
    else:
        delay = np.nan

    return delay


def compute_avg_user_metric(pkts_df, metric):
    user_metric = [metric(pkts_df[pkts_df['SrcNodeId'] == srcNodeId])
                for srcNodeId in range(numStas + 1)]

    return user_metric


def compute_norm_aggr_thr(num_stas, result):
    pkts_df = sem_utils.output_to_df(result,
                                     data_filename="packetsTrace.csv",
                                     column_sep=',',
                                     numeric_cols='all')

    thr_mbps = compute_avg_thr_mbps(pkts_df)
    aggr_rate_mbps = sem_utils.data_rate_bps_2_float_mbps(result['params']['appDataRate']) * num_stas
    norm_thr = thr_mbps / aggr_rate_mbps  # TODO check why sometimes >1
    return norm_thr


def compute_user_thr(result):
    pkts_df = sem_utils.output_to_df(result,
                                     data_filename="packetsTrace.csv",
                                     column_sep=',',
                                     numeric_cols='all')

    user_thr_mbps = compute_avg_user_metric(pkts_df, compute_avg_thr_mbps)
    return user_thr_mbps


def compute_user_avg_delay(result):
    pkts_df = sem_utils.output_to_df(result,
                                     data_filename="packetsTrace.csv",
                                     column_sep=',',
                                     numeric_cols='all')

    user_delay_ms = compute_avg_user_metric(pkts_df, compute_avg_delay_ms)
    return user_delay_ms


def compute_avg_aggr_delay_ms(result):
    pkts_df = sem_utils.output_to_df(result,
                                     data_filename="packetsTrace.csv",
                                     column_sep=',',
                                     numeric_cols='all')

    delay = compute_avg_delay_ms(pkts_df)
    return delay


def compute_std_aggr_delay_ms(result):
    pkts_df = sem_utils.output_to_df(result,
                                     data_filename="packetsTrace.csv",
                                     column_sep=',',
                                     numeric_cols='all')

    if len(pkts_df) > 0:
        delay_std_s = (pkts_df['RxTimestamp_ns'] - pkts_df['TxTimestamp_ns']).std() / 1e9 * 1e3  # [ms]
    else:
        delay_std_s = np.nan
    return delay_std_s


def compute_avg_delay_variation_ms(result):
    pkts_df = sem_utils.output_to_df(result,
                                     data_filename="packetsTrace.csv",
                                     column_sep=',',
                                     numeric_cols='all')

    if len(pkts_df) > 1:
        delay_s = (pkts_df['RxTimestamp_ns'] - pkts_df['TxTimestamp_ns']) / 1e9 * 1e3  # [ms]
        dv_s = np.mean(np.abs(np.diff(delay_s)))
    else:
        dv_s = np.nan
    return dv_s


def compute_jain_fairness(result):
    pkts_df = sem_utils.output_to_df(result,
                                     data_filename="packetsTrace.csv",
                                     column_sep=',',
                                     numeric_cols='all')

    user_thr = compute_avg_user_metric(pkts_df, compute_avg_thr_mbps)
    jain = sem_utils.jain_fairness(user_thr[1:])  # exclude user 0: AP
    return jain


###############
# Main script #
###############
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--cores",
                        help="Value of sem.parallelrunner.MAX_PARALLEL_PROCESSES. Default: 1",
                        type=int,
                        default=1)
    parser.add_argument("--paramSet",
                        help="The parameter set of a given campaign. Available: {basic, onoff, onoff_stdev}. Mandatory parameter!",
                        default='')
    parser.add_argument("--numRuns",
                        help="The number of runs per simulation. Default: 5",
                        type=int,
                        default=5)
    # baseline parameters
    parser.add_argument("--applicationType",
                        help="The baseline applicationType. Default: constant",
                        type=str,
                        default="constant")
    parser.add_argument("--normOfferedTraffic",
                        help="The baseline normOfferedTraffic. Default: 0.75",
                        type=float,
                        default=0.75)
    parser.add_argument("--socketType",
                        help="The baseline socketType. Default: ns3::UdpSocketFactory",
                        type=str,
                        default="ns3::UdpSocketFactory")
    parser.add_argument("--mpduAggregationSize",
                        help="The baseline mpduAggregationSize [B]. Default: 262143",
                        type=int,
                        default=262143)
    parser.add_argument("--phyMode",
                        help="The baseline phyMode. Default: DMG_MCS4",
                        type=str,
                        default="DMG_MCS4")
    parser.add_argument("--simulationTime",
                        help="The baseline simulationTime [s]. Default: 10.0",
                        type=float,
                        default=10.0)
    parser.add_argument("--numStas",
                        help="The baseline numStas. Default: 4",
                        type=int,
                        default=4)
    parser.add_argument("--accessCbapIfAllocated",
                        help="The baseline accessCbapIfAllocated. Default: true",
                        type=str,
                        default="true")
    parser.add_argument("--biDurationUs",
                        help="The baseline biDurationUs [us]. Default: 102400",
                        type=int,
                        default=102400)
    parser.add_argument("--onoffPeriodMean",
                        help="The baseline onoffPeriodMean [s]. Default: 0.1024",
                        type=float,
                        default=102.4e-3)
    parser.add_argument("--onoffPeriodStdev",
                        help="The baseline onoffPeriodStdev [s]. Default: 0.0",
                        type=float,
                        default=0.0)
    args = parser.parse_args()

    print('Starting sem simulation with {} core(s)...'.format(args.cores))

    sem.parallelrunner.MAX_PARALLEL_PROCESSES = args.cores
    ns_path = os.path.dirname(os.path.realpath(__file__))
    campaign_name = "BasicTest"
    script = "scheduler_comparison_qd_dense"
    campaign_dir = os.path.join(ns_path, "campaigns", "scheduler_comparison_qd_dense-" + campaign_name)
    img_dir = os.path.join(campaign_dir, 'img', args.paramSet)
    
    if not os.path.exists(img_dir):
        print("Making dir '{}'".format(img_dir))
        os.makedirs(img_dir)

    # Set up campaign
    # skip_configuration parameter is not included in the official SEM release as of December 2020
    # It was, though, included in the develop branch https://github.com/signetlabdei/sem/tree/develop
    campaign = sem.CampaignManager.new(
        ns_path, script, campaign_dir,
        overwrite=False,
        runner_type="ParallelRunner",
        optimized=True,
        skip_configuration=True,
        check_repo=False
    )

    print("campaign: " + str(campaign))

    # Set up baseline parameters
    applicationType = args.applicationType
    socketType = args.socketType
    mpduAggregationSize = args.mpduAggregationSize
    phyMode = args.phyMode
    simulationTime = args.simulationTime
    numStas = args.numStas
    allocationPeriod = [0, 1]  # 0: CbapOnly, n>0: BI/n
    accessCbapIfAllocated = args.accessCbapIfAllocated
    biDurationUs = args.biDurationUs
    onoffPeriodMean = args.onoffPeriodMean
    onoffPeriodStdev = args.onoffPeriodStdev

    appDataRate = getAppDataRate(phyMode, numStas, norm_offered_traffic=args.normOfferedTraffic)
    numRuns = args.numRuns

    if args.paramSet == 'basic':
        allocationPeriod = [0, 1, 2, 3, 4]  # 0: CbapOnly, n>0: BI/n

        norm_offered_traffic = [0.01, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1]
        appDataRate = getAppDataRate(phyMode, numStas, norm_offered_traffic)

        param_combination = run_simulations(applicationType, appDataRate, socketType, mpduAggregationSize,
                                            phyMode, simulationTime, numStas, allocationPeriod,
                                            accessCbapIfAllocated, biDurationUs, onoffPeriodMean,
                                            onoffPeriodStdev, numRuns)

        # line plots
        plot_line_metric(campaign,
                         param_combination,
                         lambda x: compute_norm_aggr_thr(numStas, x),
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel='Aggr. Throughput / Aggr. Offered Rate',
                         filename='thr_vs_aggrRate.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_aggr_delay_ms,
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel='Avg delay [ms]',
                         filename='avg_delay_vs_aggrRate.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_std_aggr_delay_ms,
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel='Delay stdev [ms]',
                         filename='delay_stdev_vs_aggrRate.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_delay_variation_ms,
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel='Avg delay variation [ms]',
                         filename='avg_delay_variation_vs_aggrRate.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_jain_fairness,
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel="Jain's Fairness Index",
                         filename='jain_fairness_vs_aggrRate.png')

        # bar plots
        assert len(bar_plots_params['numStas']) == 1, "Cannot plot bar metric over list of numStas"
        out_labels = ["AP"] + ["STA {}".format(i+1) for i in range(bar_plots_params['numStas'][0])]
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_thr,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Throughput [Mbps]",
                        filename='user_thr.png')
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_avg_delay,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Avg delay [ms]",
                        filename='user_delay.png')

    elif args.paramSet == 'onoff':
        applicationType = "onoff"
        allocationPeriod = [0, 1, 2, 3, 4]  # 0: CbapOnly, n>0: BI/n

        norm_offered_traffic = [0.01, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1]
        appDataRate = getAppDataRate(phyMode, numStas, norm_offered_traffic)

        param_combination = run_simulations(applicationType, appDataRate, socketType, mpduAggregationSize,
                                            phyMode, simulationTime, numStas, allocationPeriod,
                                            accessCbapIfAllocated, biDurationUs, onoffPeriodMean,
                                            onoffPeriodStdev, numRuns)

        # line plots
        plot_line_metric(campaign,
                         param_combination,
                         lambda x: compute_norm_aggr_thr(numStas, x),
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel='Aggr. Throughput / Aggr. Offered Rate',
                         filename='thr_vs_aggrRate.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_aggr_delay_ms,
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel='Avg delay [ms]',
                         filename='avg_delay_vs_aggrRate.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_std_aggr_delay_ms,
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel='Delay stdev [ms]',
                         filename='delay_stdev_vs_aggrRate.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_delay_variation_ms,
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel='Avg delay variation [ms]',
                         filename='avg_delay_variation_vs_aggrRate.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_jain_fairness,
                         numRuns,
                         norm_offered_traffic,
                         hue_var="allocationPeriod",
                         xlabel='Aggr. Offered Rate / PHY Rate',
                         ylabel="Jain's Fairness Index",
                         filename='jain_fairness_vs_aggrRate.png')

        # bar plots
        bar_plots_params = param_combination
        bar_plots_params['appDataRate'] = [bar_plots_params['appDataRate'][-1]]
        assert len(bar_plots_params['numStas']) == 1, "Cannot plot bar metric over list of numStas"
        out_labels = ["AP"] + ["STA {}".format(i + 1) for i in range(bar_plots_params['numStas'][0])]
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_thr,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Throughput [Mbps]",
                        filename='user_thr.png')
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_avg_delay,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Avg delay [ms]",
                        filename='user_delay.png')

    elif args.paramSet == 'onoff_stdev':
        applicationType = "onoff"
        allocationPeriod = [0, 1]  # 0: CbapOnly, n>0: BI/n
        onOffPeriodDeviationRatio = [0, 1e-3, 2e-3, 5e-3, 1e-2, 2e-2, 5e-2, 10e-2, 20e-2]
        onoffPeriodStdev = [r * onoffPeriodMean for r in onOffPeriodDeviationRatio]

        param_combination = run_simulations(applicationType, appDataRate, socketType, mpduAggregationSize,
                                            phyMode, simulationTime, numStas, allocationPeriod,
                                            accessCbapIfAllocated, biDurationUs, onoffPeriodMean,
                                            onoffPeriodStdev, numRuns)

        # line plots
        plot_line_metric(campaign,
                         param_combination,
                         lambda x: compute_norm_aggr_thr(numStas, x),
                         numRuns,
                         onOffPeriodDeviationRatio,
                         hue_var="allocationPeriod",
                         xlabel='Period Deviation Ratio',
                         ylabel='Aggr. Throughput / Aggr. Offered Rate',
                         filename='thr_vs_PeriodDeviationRatio.png',
                         xscale="log")
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_aggr_delay_ms,
                         numRuns,
                         onOffPeriodDeviationRatio,
                         hue_var="allocationPeriod",
                         xlabel='Period Deviation Ratio',
                         ylabel='Avg delay [ms]',
                         filename='avg_delay_vs_PeriodDeviationRatio.png',
                         xscale="log")
        plot_line_metric(campaign,
                         param_combination,
                         compute_std_aggr_delay_ms,
                         numRuns,
                         onOffPeriodDeviationRatio,
                         hue_var="allocationPeriod",
                         xlabel='Period Deviation Ratio',
                         ylabel='Delay stdev [ms]',
                         filename='delay_stdev_vs_PeriodDeviationRatio.png',
                         xscale="log")
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_delay_variation_ms,
                         numRuns,
                         onOffPeriodDeviationRatio,
                         hue_var="allocationPeriod",
                         xlabel='Period Deviation Ratio',
                         ylabel='Avg delay variation [ms]',
                         filename='avg_delay_variation_vs_PeriodDeviationRatio.png',
                         xscale="log")
        plot_line_metric(campaign,
                         param_combination,
                         compute_jain_fairness,
                         numRuns,
                         onOffPeriodDeviationRatio,
                         hue_var="allocationPeriod",
                         xlabel='Period Deviation Ratio',
                         ylabel="Jain's Fairness Index",
                         filename='jain_fairness_vs_PeriodDeviationRatio.png',
                         xscale="log")

        # bar plots
        bar_plots_params = param_combination
        bar_plots_params['onoffPeriodStdev'] = [bar_plots_params['onoffPeriodStdev'][-1]]
        assert len(bar_plots_params['numStas']) == 1, "Cannot plot bar metric over list of numStas"
        out_labels = ["AP"] + ["STA {}".format(i + 1) for i in range(bar_plots_params['numStas'][0])]
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_thr,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Throughput [Mbps]",
                        filename='user_thr.png')
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_avg_delay,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Avg delay [ms]",
                        filename='user_delay.png')

    elif args.paramSet == 'spPeriodicity':
        applicationType = ["constant", "onoff", "crazyTaxi", "fourElements"]
        allocationPeriod = [0, 1, 2, 3, 4]  # 0: CbapOnly, n>0: BI/n
        onoffPeriodStdev = 1e-2 * onoffPeriodMean

        param_combination = run_simulations(applicationType, appDataRate, socketType, mpduAggregationSize,
                                            phyMode, simulationTime, numStas, allocationPeriod,
                                            accessCbapIfAllocated, biDurationUs, onoffPeriodMean,
                                            onoffPeriodStdev, numRuns)

        # line plots
        plot_line_metric(campaign,
                         param_combination,
                         lambda x: compute_norm_aggr_thr(numStas, x),
                         numRuns,
                         allocationPeriod,
                         hue_var="applicationType",
                         xlabel='Allocation period (BI/n, 0=CBAP)',
                         ylabel='Aggr. Throughput / Aggr. Offered Rate',
                         filename='thr_vs_allocationPeriod.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_aggr_delay_ms,
                         numRuns,
                         allocationPeriod,
                         hue_var="applicationType",
                         xlabel='Allocation period (BI/n, 0=CBAP)',
                         ylabel='Avg delay [ms]',
                         filename='avg_delay_vs_allocationPeriod.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_std_aggr_delay_ms,
                         numRuns,
                         allocationPeriod,
                         hue_var="applicationType",
                         xlabel='Allocation period (BI/n, 0=CBAP)',
                         ylabel='Delay stdev [ms]',
                         filename='delay_stdev_vs_allocationPeriod.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_delay_variation_ms,
                         numRuns,
                         allocationPeriod,
                         hue_var="applicationType",
                         xlabel='Allocation period (BI/n, 0=CBAP)',
                         ylabel='Avg delay variation [ms]',
                         filename='avg_delay_variation_vs_allocationPeriod.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_jain_fairness,
                         numRuns,
                         allocationPeriod,
                         hue_var="applicationType",
                         xlabel='Allocation period (BI/n, 0=CBAP)',
                         ylabel="Jain's Fairness Index",
                         filename='jain_fairness_vs_allocationPeriod.png')

        # bar plots
        bar_plots_params = param_combination
        bar_plots_params['allocationPeriod'] = [bar_plots_params['allocationPeriod'][0]]
        assert len(bar_plots_params['numStas']) == 1, "Cannot plot bar metric over list of numStas"
        out_labels = ["AP"] + ["STA {}".format(i + 1) for i in range(bar_plots_params['numStas'][0])]
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_thr,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Throughput [Mbps]",
                        filename='user_thr.png')
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_avg_delay,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Avg delay [ms]",
                        filename='user_delay.png')

    elif args.paramSet == 'onoffPeriodicity':
        applicationType = "onoff"
        allocationPeriod = [0, 2]  # 0: CbapOnly, n>0: BI/n
        onoffPeriodRatio = [1, 1.75*0.5, 1.5*0.5, 1.25*0.5, 1.1*0.5, 0.5, 0.5/1.1, 0.5/1.25, 0.5/1.5, 0.5/1.75, 1/4]
        onoffPeriodMean = [r * biDurationUs/1e6 for r in onoffPeriodRatio]

        param_combination = run_simulations(applicationType, appDataRate, socketType, mpduAggregationSize,
                                            phyMode, simulationTime, numStas, allocationPeriod,
                                            accessCbapIfAllocated, biDurationUs, onoffPeriodMean,
                                            onoffPeriodStdev, numRuns)

        # line plots
        plot_line_metric(campaign,
                         param_combination,
                         lambda x: compute_norm_aggr_thr(numStas, x),
                         numRuns,
                         onoffPeriodRatio,
                         hue_var="allocationPeriod",
                         xlabel='OnOff App mean period (BI^-1)',
                         ylabel='Aggr. Throughput / Aggr. Offered Rate',
                         filename='thr_vs_onoffPeriodMean.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_aggr_delay_ms,
                         numRuns,
                         onoffPeriodRatio,
                         hue_var="allocationPeriod",
                         xlabel='OnOff App mean period (BI^-1)',
                         ylabel='Avg delay [ms]',
                         filename='avg_delay_vs_onoffPeriodMean.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_std_aggr_delay_ms,
                         numRuns,
                         onoffPeriodRatio,
                         hue_var="allocationPeriod",
                         xlabel='OnOff App mean period (BI^-1)',
                         ylabel='Delay stdev [ms]',
                         filename='delay_stdev_vs_onoffPeriodMean.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_avg_delay_variation_ms,
                         numRuns,
                         onoffPeriodRatio,
                         hue_var="allocationPeriod",
                         xlabel='OnOff App mean period (BI^-1)',
                         ylabel='Avg delay variation [ms]',
                         filename='avg_delay_variation_vs_onoffPeriodMean.png')
        plot_line_metric(campaign,
                         param_combination,
                         compute_jain_fairness,
                         numRuns,
                         onoffPeriodRatio,
                         hue_var="allocationPeriod",
                         xlabel='OnOff App mean period (BI^-1)',
                         ylabel="Jain's Fairness Index",
                         filename='jain_fairness_vs_onoffPeriodMean.png')

        # bar plots
        bar_plots_params = param_combination
        bar_plots_params['onoffPeriodMean'] = [bar_plots_params['onoffPeriodMean'][-1]]
        assert len(bar_plots_params['numStas']) == 1, "Cannot plot bar metric over list of numStas"
        out_labels = ["AP"] + ["STA {}".format(i + 1) for i in range(bar_plots_params['numStas'][0])]
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_thr,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Throughput [Mbps]",
                        filename='user_thr.png')
        plot_bar_metric(campaign,
                        param_combination,
                        compute_user_avg_delay,
                        out_labels,
                        numRuns,
                        xlabel="Node ID",
                        ylabel="Avg delay [ms]",
                        filename='user_delay.png')


    else:
        raise ValueError('paramsSet={} not recognized'.format(args.paramSet))