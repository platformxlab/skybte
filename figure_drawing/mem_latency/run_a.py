# %%
import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
import numpy as np
from matplotlib.ticker import FixedLocator

import os, sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from pyplotter.figure_utils.figure_utils import *

from matplotlib.cm import get_cmap

script_program = os.path.abspath(__file__)
script_name = os.path.basename(script_program)
script_dir = os.path.dirname(script_program)

color_arr = colors_roller_6
# color_arr = colors_test
hatch_color = "white"
hatch_arr = ["..", "//", "\\\\", "xx", ""]
# hatch_arr = ["//", "\\\\", "xx", "+", ""]

# plot font size options
plt.rc("font", size=15)
plt.rc("axes", titlesize=30)
plt.rc("xtick", labelsize=18)
plt.rc("ytick", labelsize=18)
plt.rc("legend", fontsize=18)
plt.rc("hatch", color="white")
mpl.rcParams["hatch.linewidth"] = 1.8
mpl.rcParams["pdf.fonttype"] = 42
mpl.rcParams["ps.fonttype"] = 42
mpl.rcParams.update({'font.size': 16})
mpl.rcParams.update({'font.family': 'serif'})

fig, ax0 = plt.subplots(1, 1, figsize=(14, 3.0))
plt.subplots_adjust(top = 1.02, bottom=0.01, hspace=0.6, wspace=0.20)

bar_width = 0.115
horiz_margin = 0.57
horiz_major_tick = 0.8

bar_overflow_label_fold_in = False
# ===============================================
io_file_name = f"mem_latency_breakdown"
normalize_base = "Base-CSSD"
# normalize_base = "respective"
unit = "ns"
if normalize_base == "Base-CSSD":
  ylabel = "Normalized\nAvg. Latency"
else: 
  ylabel = "Avg. Latency\nBreakdown"
x_subtick_full = True
bar_overflow_label_fold_in = True
label_normalize_column = False
label_overflow_column = False
# ===============================================

data_file = os.path.join(script_dir, f"{io_file_name}.dat")
fig_file = os.path.join(script_dir, f"{io_file_name}_{normalize_base}.pdf")

try:
  with open(data_file, "r") as f:
    lines = f.read()
except Exception as e:
  exit(1)

def should_normalize() -> bool:
  return normalize_base is not None and len(normalize_base) != 0

def normalize_respective() -> bool:
  return normalize_base == "respective"

ymin, ymax, ytick = 0, None, None
data_scale = 1

settings = []
workloads = []
measurements = []

# get settings, workloads, and measurements name
sections = [section for section in lines.strip().split("\n\n") if len(section.strip()) != 1]
settings = [setting.strip() for setting in sections[0].split("|")]
for expr_grounp in sections[1:]:
  lines = expr_grounp.strip().split("\n")
  workload = lines[0].strip()
  workloads.append(workload)
  measurements = [" ".join(line.strip().split()[0:-len(settings)]).strip() for line in lines[1:]]

# fill in data and x_tick
data_array = np.zeros((len(workloads), len(settings), len(measurements)))
x_tick_array = np.zeros((len(workloads), len(settings)))
for expr_grounp in sections[1:]:
  lines = expr_grounp.strip().split("\n")
  workload = lines[0].strip()
  workload_idx = workloads.index(workload)
  for line in lines[1:]:
    words = line.strip().split()
    measurement = " ".join(words[0:-len(settings)]).strip()
    measurement_idx = measurements.index(measurement)
    data_array[workload_idx, :, measurement_idx] = np.array([float(data.strip()) / data_scale for data in words[-len(settings):]])
    x_tick_array[workload_idx, :] = workload_idx * horiz_major_tick + (np.arange(len(settings)) - (len(settings) - 1) / 2) * bar_width

agg_slice = np.sum(data_array, axis=2)
if should_normalize():
  if not normalize_respective():
    normalize_base_idx = settings.index(normalize_base)
    normalize_base_count = agg_slice[:, normalize_base_idx]
  ytick = 0.2
  ymax = 1.33
else:
  ymax = np.nanmax(agg_slice) * 1.05
  resolution = math.pow(10, int(math.ceil(math.log10(ymax / 5))) - 2)
  resolution_ndigit = int(math.ceil(math.log10(resolution)))
  ytick = int(ymax / 5 / resolution) * resolution

for measurement_idx, measurement in enumerate(measurements):
  if measurement_idx == 0:
    bottom_slice = np.zeros(data_array[:, :, 0].shape)
  else:
    bottom_slice = np.sum(data_array[: , :, :measurement_idx], axis=2)
  for workload_idx, workload in enumerate(workloads):
    normalize_divisor = 1 if not should_normalize() else \
      agg_slice[workload_idx, :] if normalize_respective() else \
      normalize_base_count[workload_idx]
    ax0.bar(x_tick_array[workload_idx, :], data_array[workload_idx, :, measurement_idx] / normalize_divisor, color=color_arr[measurement_idx], bottom=bottom_slice[workload_idx, :] / normalize_divisor, width=bar_width, edgecolor=hatch_color, hatch=hatch_arr[measurement_idx], label=measurement, zorder=3)
    ax0.bar(x_tick_array[workload_idx, :], data_array[workload_idx, :, measurement_idx] / normalize_divisor, color="none", bottom=bottom_slice[workload_idx, :] / normalize_divisor, width=bar_width, edgecolor="#FFFFFF", linewidth=0.8, zorder=3)

x_subticks = np.ravel(x_tick_array, order='f')
if x_subtick_full:
  x_subtick_labels = [setting for setting in settings for _ in workloads]
else:
  x_subtick_labels = [setting[0].upper() for setting in settings for _ in workloads]
x_minor_ticks = np.unique(np.hstack([x_subticks - bar_width / 2, x_subticks + bar_width / 2]))
x_major_ticks = np.array([horiz_major_tick * (0.5 + x_major_tick_idx) for x_major_tick_idx in range(len(workloads) - 1)])

ax0.set_xticks(x_major_ticks)
ax0.set_xticklabels(["" for _ in x_major_ticks])
ax0.xaxis.set_minor_locator(FixedLocator(x_minor_ticks))
for x_subtick, x_subtick_label in zip(x_subticks, x_subtick_labels):
  if x_subtick_full:
    ax0.text(x_subtick + bar_width / 2, ymin - (ymax - ymin) * 0.04, x_subtick_label, ha='right', va='top', fontsize=14, rotation=60)
  else:
    ax0.text(x_subtick, ymin - (ymax - ymin) * 0.04, x_subtick_label, ha='center', va='top', fontsize=12)
for x_tick, x_tick_label in zip(np.array([horiz_major_tick * x_tick_idx for x_tick_idx in range(len(workloads))]), workloads):
  ax0.text(x_tick, ymin - (ymax - ymin) * (1.2 if not x_subtick_full else 0.42), x_tick_label, ha='center', va='top', fontsize=24)
ax0.tick_params(which='major', width=1.6, length=9)
ax0.set_xlim([-horiz_margin * horiz_major_tick, (len(workloads) - 1 + horiz_margin) * horiz_major_tick])
if should_normalize():
  yticks = np.arange(ymin, ymax-0.2, ytick)
  ax0.set_ylim([ymin, ymax])
  ax0.set_yticks(yticks)
  ax0.set_yticklabels([f"{y:.1f}" for y in yticks])
else:
  ax0.ticklabel_format(axis='y', style='sci', scilimits=(resolution_ndigit - 1, resolution_ndigit + 1))
if should_normalize:
  if label_normalize_column:
    for x_tick, data in zip(x_tick_array[:, normalize_base_idx], agg_slice[:, normalize_base_idx]):
      ax0.text(x_tick + bar_width * 0.15, 1 + 0.02 * (ymax - ymin), f"{data:.2f} {unit}", ha="center", va="bottom", rotation=90, fontsize=14)
  ax0.set_ylabel(f"{ylabel}", fontsize=24)
else:
  ax0.set_ylabel(f"{ylabel} ({unit})", fontsize=20)
ax0.yaxis.grid(zorder=0)
# ax0.yaxis.set_label_coords(-0.5, 0.47)
ymin, ymax = ax0.get_ylim()

if label_overflow_column:
  for workload_idx, workload in enumerate(workloads):
    normalize_divisor = 1 if not should_normalize() else \
      agg_slice[workload_idx, :] if normalize_respective() else \
      normalize_base_count[workload_idx]
    to_plot_array = agg_slice[workload_idx, :] / normalize_divisor
    for setting_idx, to_plot_bar in enumerate(to_plot_array):
      if to_plot_bar > ymax:
        ax0.text(x_tick_array[workload_idx, setting_idx] + bar_width * 0.6, ymax * 1.02, f"{to_plot_bar:.2f}x", ha="center", va="bottom", fontsize=14, rotation=60)

handles, labels = ax0.get_legend_handles_labels()
handles, labels = handles[0:len(handles):len(workloads)], labels[0:len(labels):len(workloads)]
# legend = ax0.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.45, 1.4), ncol=len(labels), frameon=False)
legend = ax0.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.5, 1.04), ncol=3, frameon=False, columnspacing=0.7)
ax0.hlines(0, xmin=ax0.get_xlim()[0], xmax=ax0.get_xlim()[1], zorder=9, color='black', linewidth=1)

pdf = PdfPages(fig_file)
pdf.savefig(bbox_inches="tight")
pdf.close()