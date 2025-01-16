# %%
import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import itertools

import os, sys, re

script_program = os.path.abspath(__file__)
script_name = os.path.basename(script_program)
script_dir = os.path.dirname(script_program)

sys.path.append(os.path.dirname(script_dir))
from pyplotter.figure_utils.figure_utils import *
FigurePresets.apply_default_style()

marker_arr = ["s", "o", "x", "^"]
line_styles = ["-.", "--", ":", "-"]
# hatch_color = "#78756e"
hatch_color = "white"
data_scale = 1

color_arr = colors_roller_12


dict_json = [
    {
        "Category": "H-R/W",
        "bc": "98.6",
        "bfs-dense": "83.5",
        "dlrm": "65.9",
        "radix": "82.3",
        "srad": "50.0",
        "tpcc": "60.8",
        "ycsb": "78.9"
    },
    {
        "Category": "S-R-H",
        "bc": "0.9",
        "bfs-dense": "10.7",
        "dlrm": "23.5",
        "radix": "10.3",
        "srad": "6.1",
        "tpcc": "3.3",
        "ycsb": "18.6"
    },
    {
        "Category": "S-R-M",
        "bc": "0.1",
        "bfs-dense": "3.5",
        "dlrm": "4.0",
        "radix": "1.5",
        "srad": "0.9",
        "tpcc": "0.9",
        "ycsb": "2.5"
    },
    {
        "Category": "S-W",
        "bc": "0.4",
        "bfs-dense": "2.3",
        "dlrm": "6.6",
        "radix": "5.9",
        "srad": "43.0",
        "tpcc": "35.0",
        "ycsb": "0.02"
    }
]

settings = ["H-R/W", "S-R-H", "S-R-M", "S-W"]
workloads = ["bc", "bfs-dense", "dlrm", "radix", "srad", "tpcc", "ycsb"]
data = []
for workload in workloads:
  data.append([float(dict_json[i][workload]) for i in range(len(dict_json))])

print(data)

results = {}

for workload_idx, workload in enumerate(workloads):
  results[workload] = data[workload_idx]
  
print(results)

color_arr = ["#57B4E9", "#019E73", "#E69F00", "#B21000"]

def survey(results, category_names):
    """
    Parameters
    ----------
    results : dict
        A mapping from question labels to a list of answers per category.
        It is assumed all lists contain the same number of entries and that
        it matches the length of *category_names*.
    category_names : list of str
        The category labels.
    """
    labels = list(results.keys())
    data = np.array(list(results.values()))
    data_cum = data.cumsum(axis=1)
    category_colors = plt.colormaps['RdYlGn'](
        np.linspace(0.15, 0.85, data.shape[1]))

    fig, ax = plt.subplots(figsize=(24, 5))
    ax.invert_yaxis()
    ax.xaxis.set_visible(True)
    ax.set_xticks(np.arange(0, 101, 10))
    ax.set_xticklabels(["0", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"], fontsize=28)
    ax.set_xlim(0, np.sum(data, axis=1).max())
    ax.tick_params(axis='y', labelsize=28)

    for i, (colname, color) in enumerate(zip(category_names, color_arr)):
        widths = data[:, i]
        starts = data_cum[:, i] - widths
        rects = ax.barh(labels, widths, left=starts, height=0.65,
                        label=colname, color=color)
    

    ax.legend(ncols=len(category_names), bbox_to_anchor=(0.18, 1),
              loc='lower left', fontsize=28)
    

    return fig, ax


fig, ax = survey(results, settings)

# plt.show()

fig.savefig(os.path.join(script_dir, f"output.png"), bbox_inches='tight')
fig.savefig(os.path.join(script_dir, f"output.pdf"), bbox_inches='tight')









