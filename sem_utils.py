# AUTHOR(S):
# Mattia Lecci <mattia.lecci@dei.unipd.it>
# 
# University of Padova (UNIPD), Italy
# Information Engineering Department (DEI) 
# SIGNET Research Group @ http://signet.dei.unipd.it/
# 
# Date: December 2020

import numpy as np
import pandas as pd
from matplotlib import pyplot as plt

MCS_PARAMS = {"DMG_MCS0": {"phy_rate": 27.5e6,
                           "mac_rate": 36610012},
              "DMG_MCS1": {"phy_rate": 385e6,
                           "mac_rate": 379110719,
                           "app_rate": 371355860},
              "DMG_MCS2": {"phy_rate": 770e6,
                           "mac_rate": 746778458,
                           "app_rate": 740066284},
              "DMG_MCS3": {"phy_rate": 962.5e6,
                           "mac_rate": 926434274,
                           "app_rate": 924107739},
              "DMG_MCS4": {"phy_rate": 1155e6,
                           "mac_rate": 1103569911,
                           "app_rate": 1107782893},
              "DMG_MCS5": {"phy_rate": 1251.25e6,
                           "mac_rate": 1191091513,
                           "app_rate": 1199790607},
              "DMG_MCS6": {"phy_rate": 1540e6,
                           "mac_rate": 1449796626,
                           "app_rate": 1474081443},
              "DMG_MCS7": {"phy_rate": 1925e6,
                           "mac_rate": 1785991762,
                           "app_rate": 1838998395},
              "DMG_MCS8": {"phy_rate": 2310e6,
                           "mac_rate": 2113204353,
                           "app_rate": 2202194744},
              "DMG_MCS9": {"phy_rate": 2502.5e6,
                           "mac_rate": 2273125221},
              "DMG_MCS10": {"phy_rate": 3080e6,
                            "mac_rate": 2739606669},
              "DMG_MCS11": {"phy_rate": 3850e6,
                            "mac_rate": 3332262090},
              "DMG_MCS12": {"phy_rate": 4620e6,
                            "mac_rate": 3893826210}}

def data_rate_bps_2_float_mbps(str):
    if str[-3:] == "bps" and str[:-3].isnumeric():
        return float(str[:-3]) / 1e6
    elif str[-4:] == "kbps" and str[:-4].isnumeric():
        return float(str[:-4]) / 1e3
    elif str[-4:] == "Mbps" and str[:-4].isnumeric():
        return float(str[:-4])
    elif str[-4:] == "Gbps" and str[:-4].isnumeric():
        return float(str[:-4]) *1e3
    else:
        raise ValueError("String '{}' is not a valid data rate".format(str))


def output_to_arr(result, data_filename, column_sep, row_sep='\n', columns=None, numeric_cols=None):
    """Convert generic output into np.array

    If empty or not found, return 0x0 np.array
    """

    if columns is None:
        columns = []
    if numeric_cols is None:
        numeric_cols = []
    assert numeric_cols is 'all', "Only support all int columns for improved performance"

    output = result['output']
    assert data_filename in output.keys(), "Output file not found"

    data = output[data_filename].rstrip('\n')  # remove trailing newlines
    parsed_data = [x for x in data.split(row_sep)]

    if len(parsed_data) == 0:
        # Empty output file
        return np.zeros((0,0))

    if not columns:
        columns = parsed_data[0].split(column_sep)
        data_start_idx = 1
    else:
        data_start_idx = 0

    parsed_data_len = len(parsed_data[data_start_idx:])
    if parsed_data_len > 1:
        arr = np.empty((parsed_data_len, len(columns)), dtype=int)
        for i, line in enumerate(parsed_data[data_start_idx:]):
            for j, val in enumerate(line.split(column_sep)):
                arr[i, j] = int(val)

    else:
        arr = np.zeros((0, len(columns)))

    return arr


def jain_fairness(xx):
    fairness = np.power(np.mean(xx), 2) / (np.mean(np.power(xx, 2)))
    return fairness


def bar_plot(ax, data, data_yerr=None, colors=None, total_width=0.8, single_width=1, legend=True):
    """Draws a bar plot with multiple bars per data point.
    Source: https://stackoverflow.com/a/60270421

    Parameters
    ----------
    ax : matplotlib.pyplot.axis
        The axis we want to draw our plot on.

    data: dictionary
        A dictionary containing the data we want to plot. Keys are the names of the
        data, the items is a list of the values.

        Example:
        data = {
            "x":[1,2,3],
            "y":[1,2,3],
            "z":[1,2,3],
        }

    colors : array-like, optional
        A list of colors which are used for the bars. If None, the colors
        will be the standard matplotlib color cyle. (default: None)

    total_width : float, optional, default: 0.8
        The width of a bar group. 0.8 means that 80% of the x-axis is covered
        by bars and 20% will be spaces between the bars.

    single_width: float, optional, default: 1
        The relative width of a single bar within a group. 1 means the bars
        will touch eachother within a group, values less than 1 will make
        these bars thinner.

    legend: bool, optional, default: True
        If this is set to true, a legend will be added to the axis.
    """

    # Check if colors where provided, otherwhise use the default color cycle
    if colors is None:
        colors = plt.rcParams['axes.prop_cycle'].by_key()['color']

    # Number of bars per group
    n_bars = len(data)

    # The width of a single bar
    bar_width = total_width / n_bars

    # List containing handles for the drawn bars, used for the legend
    bars = []

    # Iterate over all data
    for i, (name, values) in enumerate(data.items()):
        if (len(values.dims) == 0):
          return

        # The offset in x direction of that bar
        x_offset = (i - n_bars / 2) * bar_width + bar_width / 2

        # Draw a bar for every value of that type
        if data_yerr is None:
            for x, y in enumerate(values):
                bar = ax.bar(x + x_offset, y, width=bar_width * single_width, color=colors[i % len(colors)], label=name)
        else:
            for x, (y, yerr) in enumerate(zip(values, data_yerr[name])):
                bar = ax.bar(x + x_offset, y, yerr=yerr, width=bar_width * single_width, color=colors[i % len(colors)], label=name)

        # Add a handle to the last drawn bar, which we'll need for the legend
        bars.append(bar[0])

    # Draw legend if we need
    if legend:
        ax.legend(bars, data.keys(), loc='best')


def sta_data_rate_mbps(num_stas, phy_mode, norm_offered_traffic, mpduAggregationSize):
    assert mpduAggregationSize == 262143, f"Only max A-MPDU support (262143), requesting for {mpduAggregationSize}, instead"
    phy_rate_mbps = MCS_PARAMS[phy_mode]['app_rate'] / 1e6
    max_rate_per_sta = phy_rate_mbps / num_stas
    rate_per_sta = norm_offered_traffic * max_rate_per_sta
    return rate_per_sta