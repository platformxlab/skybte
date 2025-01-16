import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import os, sys
import argparse
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from statsFiguresUtil import *
from figureUtils import *

parser = argparse.ArgumentParser()
# parser.add_argument()
colors = colors_roller_2
colors = colors_custom1
colors = colors_roller_6
plt.rc("font", size=11)
plt.rc("xtick", labelsize=16)
plt.rc("ytick", labelsize=16)
plt.rc("legend", fontsize=18)
plt.rc("hatch", color="white")
mpl.rcParams["axes.labelsize"] = 22
mpl.rcParams["hatch.linewidth"] = 0.5
mpl.rcParams["pdf.fonttype"] = 42
mpl.rcParams["ps.fonttype"] = 42
mpl.rcParams.update({'font.size': 16})
mpl.rcParams.update({'font.family': 'serif'})


output_dir = "../../output/"

workloads_baseType3s = [
    "bc-8-baseType3",
    "bfs-dense-8-baseType3",
    "srad-8-baseType3",
    "tpcc-8-baseType3",
]


workloads_DRAMonlys = [
    "bc-8-DRAM-only-DRAM",
    "bfs-dense-8-DRAM-only-DRAM",
    "srad-8-DRAM-only-DRAM",
    "tpcc-8-DRAM-only-DRAM",
]


workload_names = [
    "bc",
    "bfs-dense",
    "srad",
    "tpcc",
]

workload_x_lims = [
    # None,
    None,
    None,
    # None,
    # None, #(0.6, 1),
    None, #(0.4, 1),
    None,
    # None,
]

workload_y_lims = [
    # (1e1, 1e4),
    (1e1, 1e5),
    (1e1, 1e5),
    # (1e1, 1e6),
    # (1e1, 1e6),
    (1e1, 1e5),
    (1e1, 1e5),
    # (1e1, 1e5),
]


def read_ltc_d(data_dir_path :str, DRAM_dir_path :str) -> tuple[dict[int, int], dict[int, int]]:
    """Returns {lat_range: count} for DRAM, CXL-SSD"""

    with open(data_dir_path, "r") as f:
        lines = f.readlines()

    lat_cdf_timestamp_lines = [l for l in lines if l.startswith("Latency_CDF_Timestamp:")]
    assert len(lat_cdf_timestamp_lines) == 1, lat_cdf_timestamp_lines
    lat_cdf_data_lines = [l for l in lines if l.startswith("Latency_CDF_Data:")]
    assert len(lat_cdf_data_lines) == 1, lat_cdf_data_lines

    lat_cdf_timestamps = [
        [int(x) for x in timestamps.strip()[len("Latency_CDF_Timestamp:"):].strip().split()]
        for timestamps in lat_cdf_timestamp_lines
    ]
    lat_cdf_datas = [
        [int(x) for x in datas.strip()[len("Latency_CDF_Data:"):].strip().split()]
        for datas in lat_cdf_data_lines
    ]
    assert len(lat_cdf_timestamps[0]) == len(lat_cdf_datas[0])
    # assert len(lat_cdf_timestamps[1]) == len(lat_cdf_datas[1])

    cxlssd_dict = {
        timestamp: count
        for timestamp, count in zip(lat_cdf_timestamps[0], lat_cdf_datas[0])
    }
    
    
    with open(DRAM_dir_path, "r") as f:
        lines = f.readlines()

    lat_cdf_timestamp_lines = [l for l in lines if l.startswith("Latency_CDF_Timestamp:")]
    assert len(lat_cdf_timestamp_lines) == 1, lat_cdf_timestamp_lines
    lat_cdf_data_lines = [l for l in lines if l.startswith("Latency_CDF_Data:")]
    assert len(lat_cdf_data_lines) == 1, lat_cdf_data_lines

    lat_cdf_timestamps = [
        [int(x) for x in timestamps.strip()[len("Latency_CDF_Timestamp:"):].strip().split()]
        for timestamps in lat_cdf_timestamp_lines
    ]
    lat_cdf_datas = [
        [int(x) for x in datas.strip()[len("Latency_CDF_Data:"):].strip().split()]
        for datas in lat_cdf_data_lines
    ]
    assert len(lat_cdf_timestamps[0]) == len(lat_cdf_datas[0])
    # assert len(lat_cdf_timestamps[1]) == len(lat_cdf_datas[1])
    
    dram_dict = {
        timestamp: count
        for timestamp, count in zip(lat_cdf_timestamps[0], lat_cdf_datas[0])
    }
    
    
    # print(f"{data_dir_path}: DRAM: {len(dram_dict)}, CXL-SSD: {len(cxlssd_dict)}")

    return dram_dict, cxlssd_dict


def plot_lat_cdf(
    lat_dicts: tuple[dict[int, int], dict[int, int]],
    bname,
    ax,
    title = "",
    xlim: tuple | None = None,
    ylim: tuple | None = None,
):
    dram_dict, cxlssd_dict = lat_dicts

    def get_cdf_data(lat_dict):
        bins, counts = zip(*lat_dict.items())
        bins = np.array(bins)
        counts = np.array(counts)
        counts = np.cumsum(counts)
        counts = counts / counts[-1]
        return bins, counts
    
    def plot_cdf_helper(lat_dict, color, label, linestyle='-'):
        bins, counts = get_cdf_data(lat_dict)
        ax.plot(
            counts,
            bins,
            linewidth=4,
            color=color,
            label=label,
            linestyle=linestyle, 
        )

    plot_cdf_helper(dram_dict, "blue", "DRAM", linestyle='--')
    plot_cdf_helper(cxlssd_dict, "red", "CXL-SSD")
    
    ax.set_yscale("log")

    xticks = list(range(0, 110, 20))
    if xlim is not None:
        if xlim[1] - xlim[0] <= 0.5:
            xticks = list(range(0, 110, 10))
        
    xlabels = [f"{x}%" for x in xticks]
    ax.set_xticks([x / 100 for x in xticks])
    ax.set_xticklabels(xlabels)

    ax.set_yticks([1e1, 1e2, 1e3, 1e4, 1e5, 1e6])
    ax.grid(True, which="major", ls="--", linewidth=0.5)


    if xlim is not None:
        ax.set_xlim(*xlim)
    else:
        ax.set_xlim(0, 1)
    if ylim is not None:
        ax.set_ylim(*ylim)

    ax.set_title(title, y=-0.32)

    # ax.legend(
    #     frameon=False,
    #     loc="upper left",
    #     # bbox_to_anchor=(0.5, 1),
    #     ncol=1,
    #     handlelength=3.5,
    #     handletextpad=0.55,
    #     columnspacing=1.8
    # )


def main():
    # fig, axs = plt.subplots(2, 3, figsize=(13, 6)) # figsize=(38, 10)
    fig, axs = plt.subplots(2, 2, figsize=(10*0.9, 7*0.9))
    plt.subplots_adjust(top=0.3, bottom=0.01, hspace=0.9, wspace=0.3)
    axs = axs.flatten()

    for bm_idx, bm_name in enumerate(workload_names):
        ax = axs[bm_idx]

        baseline_data_file = output_dir + workloads_baseType3s[bm_idx]
        DRAM_data_file = output_dir + workloads_DRAMonlys[bm_idx]
        
        lat_dicts = read_ltc_d(baseline_data_file, DRAM_data_file)
        plot_lat_cdf(
            lat_dicts,
            bm_name,
            ax,
            title=f"({chr(ord('a') + bm_idx)}) {workload_names[bm_idx]}",
            xlim=workload_x_lims[bm_idx],
            ylim=workload_y_lims[bm_idx],
        )

    # fig.suptitle(f'Latency CDF', y=0.5, x=0.1)
    handles, labels = axs[0].get_legend_handles_labels()
    fig.legend(
        labels,
        frameon=False,
        loc="upper center",
        bbox_to_anchor=(0.5, 0.99),
        ncol=6,
        handlelength=3.5,
        handletextpad=0.55,
        columnspacing=1.8
    )

    axs[0].set_ylabel("Latency (ns)")
    axs[2].set_ylabel("Latency (ns)")


    fig.tight_layout(h_pad=2, w_pad=1.5, rect=(0, 0.03, 1, 0.95))

    # extent = fig.get_window_extent().transformed(fig.dpi_scale_trans.inverted()).expanded(0.85, 1.45)
    # x_len, y_len = extent.x1 - extent.x0, extent.y1 - extent.y0
    # extent.y0 += y_len * 0.11
    # extent.y1 -= y_len * 0.6
    # extent.x0 += x_len * 0.018
    # extent.x1 -= x_len * 0.02
    output_file_dir = 'output'
    os.makedirs(output_file_dir, exist_ok=True)
    # fig.savefig(f'{output_file_dir}/LatencyCDF.png')
    fig.savefig(f'{output_file_dir}/LatencyCDF.pdf')


if __name__ == "__main__":
    main()
