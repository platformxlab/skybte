# %%
import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np

import os, sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from statsFiguresUtil import *
from figureUtils import *

hatch_arr = ["", "/", "x", "x", "-"]
# hatch_color = "#78756e"
hatch_color = "white"

color_arr = colors_dark4
color_arr = colors_dark7_2
color_arr = colors_dark6
# color_arr[1], color_arr[3] = color_arr[3], color_arr[1]
color_arr = ["#453370", "#0072B2", "#57B4E9", "#453370", "#0072B2", "#57B4E9"]
# plot font size options
plt.rc("font", size=23)
plt.rc("axes", titlesize=35)
plt.rc("xtick", labelsize=28)
plt.rc("ytick", labelsize=24)
plt.rc("legend", fontsize=27)
plt.rc("hatch", color="white")
mpl.rcParams["hatch.linewidth"] = 1.8
mpl.rcParams["pdf.fonttype"] = 42
mpl.rcParams["ps.fonttype"] = 42
mpl.rcParams.update({'font.size': 12})
mpl.rcParams.update({'font.family': 'serif'})

fig, ax0 = plt.subplots(1, 1, figsize=(20, 4.5))
plt.subplots_adjust(top = 1.02, bottom=0.01, hspace=0.6, wspace=0.6)


bar_width = 0.13
horiz_margin = 0.6
horiz_major_tick = 0.5

data_files = [
    f"breakdown/mem.txt",
    f"breakdown/compute.txt"
    
]
breakdown_names = [
    f"Bounded by Memory",
    f"Bounded by Compute"
]

all_data_arr = np.zeros(0)
for data_file_idx, data_file in enumerate(data_files):
  try:
    with open(data_file, "r") as f:
      lines = f.read()
  except Exception as e:
    exit(1)

  settings = []
  workloads = []
  sections = lines.strip().split("\n\n")
  settings = [setting.strip() for setting in sections[0].split("|")]
  data_array = np.zeros((len(sections) - 1, len(settings)))
  x_tick_array = np.zeros((len(sections) - 1, len(settings)))
  for section_idx, section in enumerate(sections[1:]):
    lines = section.strip().split("\n")
    workload = lines[0].strip()
    workloads.append(workload)
    data_array[section_idx, :] = np.array([float(data) for data in lines[1].split()])
    x_tick_array[section_idx, :] = section_idx * horiz_major_tick + (np.arange(len(settings)) - (len(settings) - 1) / 2) * bar_width
  if len(all_data_arr.shape) == 1:
    all_data_arr = np.zeros((*data_array.shape, len(data_files)))
  all_data_arr[:, :, data_file_idx] = data_array
sum_slice = np.sum(all_data_arr, axis=2)
for data_file_idx, data_file in enumerate(data_files):
  all_data_arr[:, :, data_file_idx] /= sum_slice
  for j, setting in enumerate(settings):
    if data_file_idx == 0:
      bottom_slice = np.zeros(all_data_arr[:, :, 0].shape)
    else:
      bottom_slice = np.sum(all_data_arr[:, :, :data_file_idx], axis=2)
    ax0.bar(x_tick_array[:, j], all_data_arr[:, j, data_file_idx], color=color_arr[data_file_idx], bottom=0.0055 + bottom_slice[:, j], width=bar_width, edgecolor=hatch_color, hatch=hatch_arr[data_file_idx], label=breakdown_names[data_file_idx], zorder=3)

    ax0.bar(x_tick_array[:, j], all_data_arr[:, j, data_file_idx], color="none", bottom=0.0055 + bottom_slice[:, j], width=bar_width, edgecolor="white", linewidth=0.8, zorder=3)
  # for x_tick, data in zip(x_tick_array[:, 0], data_array[:, 0]):
  #   ax.text(x_tick, 1.05, f"{data:5.2f} kops/s", ha="center", va="bottom", rotation=90, fontsize=10)

arrow_width, arrow_head_width, arrow_fontsize = 2, 8, 23
# ax0.annotate("Core 0",
#   xy=(8 * horiz_major_tick - bar_width * 1.5, 1), 
#   xytext=(8 * horiz_major_tick - bar_width * 1.5 - 0.7, 1.02),
#   arrowprops=dict(facecolor='black', shrink=0., width=arrow_width, headwidth=arrow_head_width, headlength=4, visible=False),
#   fontsize=arrow_fontsize, linespacing=0,
#   bbox=dict(boxstyle="square,pad=0", fc="w", ec="none", alpha=0.),
# )
# ax0.annotate("",
#   xy=(8 * horiz_major_tick - bar_width * 1.5, 1), 
#   xytext=(8 * horiz_major_tick - bar_width * 1.5 - 0.26, 1.04),
#   arrowprops=dict(facecolor='black', shrink=0., width=arrow_width, headwidth=arrow_head_width, headlength=4),
#   fontsize=arrow_fontsize, linespacing=0,
#   bbox=dict(boxstyle="square,pad=0", fc="w", ec="none", alpha=0.),
# )
ax0.annotate("DRAM",
  xy=(6 * horiz_major_tick - bar_width * 0.5, 1), 
  xytext=(6 * horiz_major_tick - bar_width * 1.5 - 0.25, 1.12),
  arrowprops=dict(facecolor='black', shrink=0., width=arrow_width, headwidth=arrow_head_width, headlength=4),
  fontsize=arrow_fontsize, linespacing=0,
  bbox=dict(boxstyle="square,pad=0", fc="w", ec="none", alpha=0.),
)
ax0.annotate("CXL-SSD",
  xy=(6 * horiz_major_tick + bar_width * 0.5, 1), 
  xytext=(6 * horiz_major_tick - bar_width * 1.5 + 0.05, 1.16),
  arrowprops=dict(facecolor='black', shrink=0., width=arrow_width, headwidth=arrow_head_width, headlength=4),
  fontsize=arrow_fontsize, linespacing=0,
  bbox=dict(boxstyle="square,pad=0", fc="w", ec="none", alpha=0.),
)
# ax0.annotate("Core 3",
#   xy=(8 * horiz_major_tick + bar_width * 1.5, 1), 
#   xytext=(8 * horiz_major_tick - bar_width * 1.5 + 0.4, 1.12),
#   arrowprops=dict(facecolor='black', shrink=0., width=arrow_width, headwidth=arrow_head_width, headlength=4),
#   fontsize=arrow_fontsize, linespacing=0,
#   bbox=dict(boxstyle="square,pad=0", fc="w", ec="none", alpha=0.),
# )
ax0.set_xticks(np.arange(len(workloads)) * horiz_major_tick)
ax0.set_xticklabels([workload.replace("|", "\n") for workload in workloads])
ax0.set_xlim([-horiz_margin * horiz_major_tick, (len(workloads) - 1 + horiz_margin) * horiz_major_tick])
ax0.set_ylim([0, 1.3])

yticks = [0, 0.25, 0.5, 0.75, 1]
yticks = [0, 0.2, 0.4, 0.6, 0.8, 1]
ax0.set_yticks(yticks)
ax0.set_yticklabels([str(y) for y in yticks])
ax0.set_ylabel("Execution Time", fontsize = 30)
ax0.yaxis.set_label_coords(-0.07, 0.4)
ax0.yaxis.set_major_formatter(mpl.ticker.PercentFormatter(xmax=1))
ax0.yaxis.grid(zorder=0)
ax0.hlines(0, xmin=ax0.get_xlim()[0], xmax=ax0.get_xlim()[1], zorder=9, color='black', linewidth=1)
  
handles, labels = ax0.get_legend_handles_labels()
handles, labels = handles[0:len(handles):len(settings)], labels[0:len(labels):len(settings)]
legend = ax0.legend(handles, labels, frameon=False, loc="upper left", ncol=2, bbox_to_anchor=(0.00, 1.03), handletextpad=0.55, columnspacing=1.5)

# shift = max([t.get_window_extent().width for t in legend.get_texts()])
# for t in legend.get_texts():
#     t.set_position(((shift - t.get_window_extent().width) / 2 - 6, 0))

plt.show()

extent = fig.get_window_extent().transformed(fig.dpi_scale_trans.inverted()).expanded(0.85, 1.45)
x_len, y_len = extent.x1 - extent.x0, extent.y1 - extent.y0
extent.y0 -= y_len * 0.04
extent.y1 -= y_len * 0.1
extent.x0 -= x_len * 0.03
extent.x1 -= x_len * 0.02
# figname = list(net_name_translation.values())[model]
# fig.savefig(f"OverallPerf{figname}.png", bbox_inches=extent)
fig.savefig(f"output/Breakdown.png", bbox_inches=extent)
fig.savefig(f"output/Breakdown.pdf", bbox_inches=extent)
