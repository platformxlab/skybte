import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np

import os, sys

script_program = os.path.abspath(__file__)
script_name = os.path.basename(script_program)
script_dir = os.path.dirname(script_program)

sys.path.append(os.path.dirname(script_dir))
from pyplotter.figure_utils.figure_utils import *

import math

normalize_base = None
normalize_base = "Base-CSSD"

# hatch_color = "#78756e"
hatch_color = "white"

color_arr = colors_roller_6
hatch_arr = ["", "-", "/", "\\", "x", ".", "|", "O", "*", "0", "-", ""]

# plot font size options
plt.rc("font", size=11)
plt.rc("xtick", labelsize=23)
plt.rc("ytick", labelsize=15)
plt.rc("legend", fontsize=18)
plt.rc("hatch", color="white")
mpl.rcParams["axes.labelsize"] = 22
mpl.rcParams["hatch.linewidth"] = 1.8
mpl.rcParams["pdf.fonttype"] = 42
mpl.rcParams["ps.fonttype"] = 42
mpl.rcParams.update({'font.size': 16})
mpl.rcParams.update({'font.family': 'serif'})

fig, ax0 = plt.subplots(1, 1, figsize=(14, 2.5))
plt.subplots_adjust(top = 1.02, bottom=0.01, hspace=0.6, wspace=0.20)

data_file = os.path.join(script_dir, f"wr.dat")
output_file = os.path.join(script_dir, "nwrites")

yscale_log = True
log_lower_bound = 10 ** -(3.5)

bar_width = 0.092
horiz_margin = 0.65
horiz_major_tick = 0.75
try:
  with open(data_file, "r") as f:
    lines = f.read()
except Exception as e:
  print(f"Data file {data_file} not found")
  exit(1)

settings = []
workloads = []
sections = lines.strip().split("\n\n")
settings = [setting.strip() for setting in sections[0].split("|")]
data_array = np.zeros((len(sections) - 1, len(settings)))
settings = settings
x_tick_array = np.zeros((len(sections) - 1, len(settings)))
for section_idx, section in enumerate(sections[1:]):
  lines = section.strip().split("\n")
  workload = lines[0].strip()
  workloads.append(workload)
  data_array[section_idx, :] = np.array([float(data) for data in lines[1].split()])
  x_tick_array[section_idx, :] = section_idx * horiz_major_tick + (np.arange(len(settings)) - (len(settings) - 1) / 2) * bar_width
for j, setting in enumerate(settings):
  if normalize_base:
    base_array = data_array[:, settings.index(normalize_base)]
  else:
    base_array = np.ones(data_array.shape[0])
  ax0.bar(x_tick_array[:, j], data_array[:, j] / base_array, color=color_arr[j], width=bar_width, edgecolor=hatch_color, hatch=hatch_arr[j], label=setting, zorder=3)
  ax0.bar(x_tick_array[:, j], data_array[:, j] / base_array, color="none", width=bar_width, edgecolor="white", linewidth=0.8, zorder=3)
# for x_tick, data in zip(x_tick_array[:, 0], data_array[:, 0]):
#   ax.text(x_tick, 1.05, f"{data:5.2f} kops/s", ha="center", va="bottom", rotation=90, fontsize=10)

ax0.set_xticks(np.arange(len(workloads)) * horiz_major_tick)
ax0.set_xticklabels([workload.replace("|", "\n") for workload in workloads])
ax0.set_xlim([-horiz_margin * horiz_major_tick, (len(workloads) - 1 + horiz_margin) * horiz_major_tick])
ax0.set_yticks(np.arange(0, 1.2, 0.2))
if yscale_log:
  ax0.set_yscale("log")
  lower_bound = math.log10(log_lower_bound)
  upper_bound_effective = 1.35
  upper_bound = -lower_bound * (upper_bound_effective - 1)
  ax0.set_ylim([10 ** lower_bound, 10 ** upper_bound])
  ax0.set_yticks([0.001, 0.01, 0.1, 1])
else:
  ax0.set_ylim([0, 1.2])

# ax0.set_ylabel("Normalized\nPerformance", fontsize=20)
if normalize_base is not None:
  y_label = "Normalized\nNumber of Writes"
else:
  y_label = "#Writes"
ax0.set_ylabel(y_label, fontsize=21)
# ax0.yaxis.set_label_coords(-0.06, 0.4)
# ax0.hlines(y=1, xmin=ax0.get_xlim()[0], xmax=ax0.get_xlim()[1], colors="grey", linestyles="--")
ax0.yaxis.grid(zorder=0)
ax0.hlines(0, xmin=ax0.get_xlim()[0], xmax=ax0.get_xlim()[1], zorder=9, color='black', linewidth=1)

handles, labels = ax0.get_legend_handles_labels()
legend = ax0.legend(handles, labels, frameon=False, loc="upper center", bbox_to_anchor=(0.5, 1.05), ncol=4, columnspacing=1.8, fontsize=15)

fig.savefig(os.path.join(script_dir, f"{output_file}.png"), bbox_inches='tight')
fig.savefig(os.path.join(script_dir, f"{output_file}.pdf"), bbox_inches='tight')
