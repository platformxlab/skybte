import os, sys, getopt, re

script_program = os.path.abspath(__file__)
script_name = os.path.basename(script_program)
script_dir = os.path.dirname(script_program)

# START customization
figure_drawing_dir = script_dir
base_dir = os.path.abspath(os.path.join(figure_drawing_dir, os.pardir))
output_dir = os.path.abspath(os.path.join(base_dir, "output"))
# END customization

# START requirement
default_output_folder_tag = ""
output_folder_pardir = output_dir
# END requirement

def usage():
    print(f"Usage: {script_name}:")
    print(f"Run simulation")
    print(f"  -h             print help, this message")
    print(f"  -v             verbose")
    print(f"  -t             ")
    print(f"  -d             ")
    print(f"  -f             ")
    print(f"Return values")
    print(f"  0              script terminates correctly")
    print(f"  1              invalid options")

output_folder_tag_pattern = r"^[0-9a-zA-Z\-]*$"

verbose = False
output_folder_tag = ""
target_dir_rel = None
desc_filename = None
try:
    opts, args = getopt.getopt(sys.argv[1:],
    "hvt:d:f:",
    ["help", "verbose", "target_folder=", "description_filename="])
except getopt.GetoptError as err:
  # print help information and exit:
  print(err)  # will print something like "option -a not recognized"
  usage()
  sys.exit(1)
for arg, opt_arg in opts:
    if arg in ("-h", "--help"):
        usage()
        sys.exit()
    elif arg in ("-v", "--verbose"):
        verbose = True
    elif arg in ("-t", "--output_folder_tag"):
        output_folder_tag = opt_arg
        if re.match(output_folder_tag_pattern, output_folder_tag) is None:
            print(f"{output_folder_tag_pattern} does not match regex {output_folder_tag_pattern}")
    elif arg in ("-d", "--target_folder"):
        target_dir_rel = opt_arg
    elif arg in ("-f", "--description_filename"):
        desc_filename = opt_arg
    else:
        print(f"Unhandled option <{arg}>")
        sys.exit(1)

# parameter validation BEGIN
if len(output_folder_tag) != 0:
  output_dir_rel = os.path.join(output_folder_pardir, output_folder_tag)
else:
  output_dir_rel = os.path.join(output_folder_pardir, default_output_folder_tag)
output_dir = os.path.join(output_folder_pardir, output_dir_rel)
if verbose:
    print(f"Output folder tag: {output_folder_tag if len(output_folder_tag) else '<no tag>'}")
    print(f"Output folder:     {output_dir}")
assert os.path.isdir(output_dir), f"Output folder <{output_dir}> not found"

assert target_dir_rel is not None, "Please specify a target folder"
target_dir = os.path.abspath(os.path.join(script_dir, target_dir_rel))
assert os.path.isdir(target_dir), f"Target folder {target_dir_rel} ({target_dir}) does not exist"
if verbose:
    print(f"Target folder:     {target_dir}")

import pyplotter.utils.parser.generic_parser as gp
desc_filename = gp.default_description_name if desc_filename is None else desc_filename
assert os.path.isdir(target_dir), f"Description file {desc_filename} does not exist in target folder {target_dir_rel} ({target_dir})"
# parameter validation END

import pyplotter.utils.parser.section_parser as sp
import pyplotter.utils.multidim_repr as mdr
import pyplotter.utils.generator.legacy_data_generator as ldg
import pyplotter.utils.generator.latex_table_generator as ltg

from typing import Callable, Any

import numpy as np

np.set_printoptions(linewidth=200, formatter={'float_kind':'{:20f}'.format})

def parse_line(line: str, state: dict) -> list[tuple[str, Any]]:
    stats: list[tuple[str, Any]] = []
    # RW modification table parse
    if "rw_mod" in state:
        rw_pattern = r"(\d+)%:\s*r\s*(\-?\d*\.\d+|\-?nan)%,\s*w\s*(\-?\d*\.\d+|\-?nan)%"
        in_rw_mod = state["rw_mod"]
        if in_rw_mod:
            rmatches = re.findall(rw_pattern, line)
            if len(rmatches) == 0:
                state["rw_mod"] = False
            else:
                for rmatch in rmatches:
                    modification_ncl = round(float(rmatch[0]) * 64 / 100)
                    r_ratio = np.nan if "nan" in rmatch[1] else float(rmatch[1]) / 100
                    w_ratio = np.nan if "nan" in rmatch[2] else float(rmatch[1]) / 100
                    stats.append((f"r_modified_{1 * modification_ncl / 64}", r_ratio))
                    stats.append((f"w_modified_{1 * modification_ncl / 64}", w_ratio))
        elif "ByteFS rw modification distribution" in line:
            state["rw_mod"] = True
        return stats
    # other stats
    if line.startswith("**"):
        all_numbers = re.findall(r'\d*\.?\d+', line)
        stats.append((f"core_exe_time{all_numbers[0]}", float(all_numbers[2])))
    elif line.startswith("|        0           0 /           0 =   -nan% |"):
        all_numbers = re.findall(r'\d*\.?\d+', line)
        stats.append(("avg_host_hit_latency", float(all_numbers[3])))
        stats.append(("avg_log_read_latency", float(all_numbers[4])))
        stats.append(("avg_log_write_latency", float(all_numbers[5])))
        stats.append(("avg_cache_hit_latency", float(all_numbers[6])))
        stats.append(("avg_cache_miss_latency", float(all_numbers[7])))
        stats.append(("avg_total_latency", float(all_numbers[8])))
    else:
        stats += sp.default_line_parser(line)
    return stats

def parse_final_data(data: dict[str, Any], state: dict) -> dict[str, Any]:
    core_exe_times = [
        v for k, v in data.items() if k.startswith("core_exe_time")
    ]
    data["core_exe_time_avg"] = np.average(core_exe_times) if len(core_exe_times) else 0
    data["core_exe_time_max"] = max(core_exe_times) if len(core_exe_times) else 0
    return data

settings = {
    "baseType3" : "Base-CSSD",
    "flatflash" : "SkyByte-P",
    "assd-C-rr" : "SkyByte-C",
    "assd-W"    : "SkyByte-W",
    "assd-CA"   : "AstriFlash-CXL",
    "assd-CT"   : "SkyByte-CT",
    "assd-CP"   : "SkyByte-CP",
    "assd-WCT"   : "SkyByte-WCT",
    "assd-WP"   : "SkyByte-WP",
    "assd-Full-rr" : "SkyByte-Full",
    "assd-WP-DRAM"   : "DRAM-Only"
}

workloads = {
    "bc-8"         : "bc",
    "bc-24"        : "bc",
    "bfs-dense-8"  : "bfs-dense",
    "bfs-dense-24" : "bfs-dense",
    "dlrm-8"       : "dlrm",
    "dlrm-24"      : "dlrm",
    "radix-8"      : "radix",
    "radix-24"     : "radix",
    "srad-8"       : "srad",
    "srad-48"      : "srad",
    "tpcc-8"       : "tpcc",
    "tpcc-24"      : "tpcc",
    "ycsb-8"       : "ycsb",
    "ycsb-24"      : "ycsb",
    # "ycsbB-8"       : "ycsb-b",
    # "ycsbB-24"      : "ycsb-b",
}


if any([option in target_dir for option in ["e2e_perf", "nwrites", "mem_latency", "alter_e2e_perf"]]):

    if "e2e_perf" in target_dir:
        settings = {
            "baseType3" : "Base-CSSD",
            "flatflash" : "SkyByte-P",
            "assd-C-rr" : "SkyByte-C",
            "assd-W"    : "SkyByte-W",
            "assd-CP"   : "SkyByte-CP",
            "assd-WP"   : "SkyByte-WP",
            "assd-Full-rr" : "SkyByte-Full",
            "assd-WP-DRAM"   : "DRAM-Only"
        }
        
    if "mem_latency" in target_dir:
        settings = {
            "baseType3" : "Base-CSSD",
            "flatflash" : "SkyByte-P",
            "assd-W"    : "SkyByte-W",
            "assd-WP"   : "SkyByte-WP",
            "assd-Full-rr" : "SkyByte-Full",
            "assd-WP-DRAM"   : "DRAM-Only"
        }


from pyplotter.utils.parser.serialize_parser import SerializeParser
sparser = SerializeParser()
sparser.register(list(settings.keys()))
sparser.register(list(workloads.keys()))

# Generate generic dataset as all.dat
dataset = mdr.NamedArray.from_dim_names([
    "settings",
    "workloads",
    "measurements"
])
for stat_file in os.listdir(output_dir):
    if any([s in stat_file for s in ("txt", "DRAM")]):
        continue
    remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file)
    if len(remaining) > 0 or this_setting is None or this_workload is None:
        continue
    this_setting = settings[this_setting]
    this_workload = workloads[this_workload]
    stat_parser = sp.StatParser(parse_line, parse_final_data)
    stat_parser_wrapper = sp.SectionParsers(None)
    stat_parser_wrapper.register_section_parser(stat_parser)
    with open(os.path.join(output_dir, stat_file), "r") as fin:
        stat_parser_wrapper.parse(fin)
    for k, v in stat_parser.get_dataset().items():
        dataset.insert_data([this_setting, this_workload, k], v)
dataset.set_sort_rules("settings",
    lambda arr: sorted(
        arr,
        key=lambda setting: list(settings.values()).index(setting)
    )
)
dataset.set_sort_rules("workloads",
    lambda arr: sorted(
        arr,
        key=lambda workload: list(workloads.values()).index(workload)
    )
)
dataset.sort()
with open("all.dat", "w") as fout:
    if verbose:
        dataset.dump(fout, indent=2)
    else:
        dataset.dump(fout)
del dataset

if any([option in target_dir for option in ["e2e_perf", "nwrites", "mem_latency", "alter_e2e_perf"]]):

    with open("all.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    if "e2e_perf" in target_dir:
        result = dataset.get_data_slice([None, None, "core_exe_time_avg"]).astype(float)
        result_names = dataset.get_setting_name_slice([None, None, "core_exe_time_avg"])
        with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
            ldg.dump(result_names, result, fout)

        result = dataset.get_data_slice([None, None, "core_exe_time_max"])
        result_names = dataset.get_setting_name_slice([None, None, "core_exe_time_max"])
        # with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        print(result_names)
        print(result)
            # ldg.dump(result_names, result, fout)
            
    elif "alter_e2e_perf" in target_dir:
        result = dataset.get_data_slice([None, None, "core_exe_time_avg"]).astype(float)
        result_names = dataset.get_setting_name_slice([None, None, "core_exe_time_avg"])
        with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
            ldg.dump(result_names, result, fout)

        result = dataset.get_data_slice([None, None, "core_exe_time_max"])
        result_names = dataset.get_setting_name_slice([None, None, "core_exe_time_max"])
        with open(os.path.join(target_dir, "max.dat"), "w") as fout:
            print(result_names)
            print(result)
            ldg.dump(result_names, result, fout)

    elif "nwrites" in target_dir:
        result = dataset.get_data_slice([None, None, "total_nand_wr_user"]).astype(float) + dataset.get_data_slice([None, None, "total_nand_wr_internal"]).astype(float)
        result_names = dataset.get_setting_name_slice([None, None, "total_nand_wr_user"])
        with open(os.path.join(target_dir, "wr.dat"), "w") as fout:
            ldg.dump(result_names, result, fout)

    elif "mem_latency" in target_dir:
        n_mem_access = dataset.get_data_slice(
            [None, None, "number_of_memory_accesses"]).astype(float)
        n_host_hit = dataset.get_data_slice(
            [None, None, "number_of_memory_accesses_host_dram_hit"]).astype(float)
        n_log_read = dataset.get_data_slice(
            [None, None, "number_of_memory_accesses_log_read"]).astype(float)
        n_log_write = dataset.get_data_slice(
            [None, None, "number_of_memory_accesses_log_write"]).astype(float)
        n_ssd_hit = dataset.get_data_slice(
            [None, None, "number_of_memory_accesses_ssd_cache_hit"]).astype(float)
        n_ssd_miss = dataset.get_data_slice(
            [None, None, "number_of_memory_accesses_ssd_cache_miss"]).astype(float)

        host_hit_latency = dataset.get_data_slice(
            [None, None, "avg_host_hit_latency"]).astype(float)
        log_read_latency = dataset.get_data_slice(
            [None, None, "avg_log_read_latency"]).astype(float)
        log_write_latency = dataset.get_data_slice(
            [None, None, "avg_log_write_latency"]).astype(float)
        cache_hit_latency = dataset.get_data_slice(
            [None, None, "avg_cache_hit_latency"]).astype(float)
        cache_miss_latency = dataset.get_data_slice(
            [None, None, "avg_cache_miss_latency"]).astype(float)
        total_latency = dataset.get_data_slice(
            [None, None, "overall_average_latency"]).astype(float)

        result_names = dataset.get_setting_name_slice([None, None, "total_nand_wr_user"])
        flatflash_idx = result_names[0].index("SkyByte-P")
        basetype3_idx = result_names[0].index("Base-CSSD")
        # group_str_idx = ["bc", "dlrm", "radix"]
        group_str_idx = ["dlrm", "radix"]
        group_int_idx = [result_names[1].index(str_idx) for str_idx in group_str_idx]

        # save for later use
        all_total_latency = total_latency * n_mem_access

        WP_total_mem_access = dataset["SkyByte-WP", group_str_idx, "number_of_memory_accesses"].astype(float)
        flatflash_total_mem_access = dataset["SkyByte-P", group_str_idx, "number_of_memory_accesses"].astype(float)
        basetype3_total_mem_access = dataset["Base-CSSD", group_str_idx, "number_of_memory_accesses"].astype(float)
        n_host_hit[flatflash_idx, group_int_idx] -= (flatflash_total_mem_access - WP_total_mem_access)
        n_ssd_hit[basetype3_idx, group_int_idx] -= (basetype3_total_mem_access - WP_total_mem_access)
        n_mem_access[flatflash_idx, group_int_idx] -= (flatflash_total_mem_access - WP_total_mem_access)
        n_mem_access[basetype3_idx, group_int_idx] -= (basetype3_total_mem_access - WP_total_mem_access)

        cxl_latency = 40
        ssd_dram_latency = 46
        indexing_latency = 72

        all_total_latency[flatflash_idx, group_int_idx] -= (flatflash_total_mem_access - WP_total_mem_access) * host_hit_latency[flatflash_idx, group_int_idx]
        all_total_latency[basetype3_idx, group_int_idx] -= (basetype3_total_mem_access - WP_total_mem_access) * (cxl_latency + indexing_latency + ssd_dram_latency)

        total_latency = all_total_latency / n_mem_access
        host_dram = n_host_hit * host_hit_latency
        cxl_protocol = (n_log_read + n_log_write + n_ssd_hit + n_ssd_miss) * cxl_latency
        indexing = (n_log_read + n_ssd_hit + n_ssd_miss) * indexing_latency
        ssd_dram = (n_log_read + n_log_write + n_ssd_hit + n_ssd_miss) * ssd_dram_latency
        # flash_latency = n_ssd_miss * (cache_miss_latency - cxl_latency - indexing_latency - ssd_dram_latency)
        flash_latency = total_latency * n_mem_access - host_dram - cxl_protocol - indexing - ssd_dram
        total = host_dram + cxl_protocol + indexing + ssd_dram + flash_latency

        with open(os.path.join(target_dir, "avg_mem_latency.dat"), "w") as fout:
            ldg.dump(result_names, total_latency, fout, verbose=True)

        result = np.stack((host_dram, cxl_protocol, indexing, ssd_dram, flash_latency), axis=2)
        for idx in range(result.shape[-1]):
            result[:, :, idx] = result[:, :, idx] / total * total_latency
        result_names.append(["Host DRAM", "CXL Protocol", "Indexing", "SSD DRAM", "Flash"])
        with open(os.path.join(target_dir, "mem_latency_breakdown.dat"), "w") as fout:
            ldg.dump(result_names, result, fout, verbose=True)

        # print(ltg.dumps([result_name for result_name in result_names[1:]], result[0, :, :], verbose=True))
        # with open(os.path.join(target_dir, "mem_latency_breakdown.latex"), "w") as fout:
        #     ltg.dump(result_names, result, fout, verbose=True)

elif any([option in target_dir for option in ["sensitivity_dram", "sensitivity_dram_alt"]]):

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "dram_size",
        "measurements"
    ])

    # [settings.pop(k) for k in [k for k in settings if settings[k] in ["SkyByte-P", "SkyByte-W", "SkyByte-Full"]]]
    # [workloads.pop(k) for k in [k for k in workloads if workloads[k] in ["bfs-dense"]]]
    sparser.reregister(list(settings.keys()), 0)
    sparser.reregister(list(workloads.keys()), 1)
    sparser.register(r"main([\d_]+)g-([\d_]+)-w([\d_]+)", regex=True)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file, ndim=2, persist=True)
        # if len(remaining) == 0:
        #     sparser.clear_matches()
        #     if this_setting is None or this_workload is None:
        #         continue
        #     dram_size = "2GB"

        # else:
        remaining, (this_setting, this_workload, this_log_size), (_, _, aux_log_size) = sparser.recognize(remaining)
        if len(remaining) > 0 or this_setting is None or this_workload is None or aux_log_size is None:
            continue

        dram_size_info = [float(x.replace("_", ".")) for x in aux_log_size]
        if not np.isclose(dram_size_info[1], 0.25) or not np.isclose(dram_size_info[2], 0.25):
            continue
        dram_size = dram_size_info[0]
        if np.isclose(dram_size, 16):
            continue
        is_whole = np.isclose(dram_size, round(dram_size))
        dram_size = f"{int(dram_size) if is_whole else f'{dram_size:.1f}'}GB"

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, dram_size, k], v)

    dataset.set_sort_rules("settings",
        lambda arr: sorted(
            arr,
            key=lambda setting: list(settings.values()).index(setting)
        )
    )
    dataset.set_sort_rules("dram_size",
        lambda arr: sorted(arr, key=lambda log_size: [
            rmatch := re.search(r"^[^\d]*(\d*\.?\d+)", log_size),
            0 if rmatch is None else (float(rmatch.group(0)) if len(rmatch.groups()) > 0 else 0)
        ][-1])
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    with open("sen.dram.dat", "w") as fout:
        if verbose:
            dataset.dump(fout, indent=2)
        else:
            dataset.dump(fout)

    del dataset

    with open("sen.dram.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    if "sensitivity_dram_alt" in target_dir:
        result = dataset[:, :, :, "core_exe_time_avg"].astype(float)
        result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "core_exe_time_avg"))
        with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
            ldg.dump(result_names, result, fout, verbose=True)

        result = dataset[:, :, :, "core_exe_time_max"].astype(float)
        result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "core_exe_time_max"))
        with open(os.path.join(target_dir, "max.dat"), "w") as fout:
            ldg.dump(result_names, result, fout, verbose=True)

    elif "sensitivity_dram" in target_dir:
        result = dataset["SkyByte-WP", :, :, "core_exe_time_avg"].astype(float).T
        result_names = dataset.get_setting_name_slice_arr(("SkyByte-WP", ..., ..., "core_exe_time_avg"))[::-1]
        with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
            ldg.dump(result_names, result, fout, verbose=True)

        result = dataset["SkyByte-WP", :, :, "core_exe_time_max"].astype(float).T
        result_names = dataset.get_setting_name_slice_arr(("SkyByte-WP", ..., ..., "core_exe_time_max"))[::-1]
        with open(os.path.join(target_dir, "max.dat"), "w") as fout:
            ldg.dump(result_names, result, fout, verbose=True)




elif "m_sensitivity_write_log_miss_ratio" in target_dir:

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "log_size",
        "measurements"
    ])

    # workloads.pop("bfs-dense-8")
    sparser.reregister(list(workloads.keys()), 1)
    sparser.register(r"main(\d+)g-([\d_]+)-w([\d_]+)", regex=True)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file, ndim=2, persist=True)
        if len(remaining) == 0:
            sparser.clear_matches()
            if this_setting is None or this_workload is None:
                continue
            log_size = "64MB"

        else:
            remaining, (this_setting, this_workload, this_log_size), (_, _, aux_log_size) = sparser.recognize(remaining)
            if len(remaining) > 0 or this_setting is None or this_workload is None or aux_log_size is None:
                continue

            dram_size_info = [float(x.replace("_", ".")) for x in aux_log_size]
            if not np.isclose(dram_size_info[0], 2) or not np.isclose(dram_size_info[1], 0.25):
                continue
            log_size = f"{round(dram_size_info[0] * dram_size_info[1] * dram_size_info[2] * 1024)}MB"
            if log_size == "0MB":
                log_size = "0.5MB"

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]
        if this_setting != "SkyByte-WP":
            continue

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, log_size, k], v)

    dataset.set_sort_rules("log_size",
            lambda arr: sorted(arr, key=lambda log_size: [
            rmatch := re.search(r"^[^\d]*(\d*\.?\d+)", log_size),
            0 if rmatch is None else (float(rmatch.group(0)) if len(rmatch.groups()) > 0 else 0)
        ][-1])
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    with open("sen.log.dat", "w") as fout:
        if verbose:
            dataset.dump(fout, indent=2)
        else:
            dataset.dump(fout)

    del dataset

    with open("sen.log.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    result = (dataset[:, :, :, "number_of_memory_accesses_ssd_cache_miss"].astype(float).T + dataset[:, :, :, "number_of_memory_accesses_log_read"].astype(float).T) / (dataset[:, :, :, "number_of_memory_accesses"].astype(float).T - dataset[:, :, :, "number_of_memory_accesses_host_dram_hit"].astype(float).T)
    np.set_printoptions(linewidth=200, formatter={'float_kind':'{:20f}'.format})
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "write_operation"))[::-1]
    print(result_names)
    print(result)
    with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    result = (dataset[:, :, :, "number_of_memory_accesses_ssd_cache_miss"].astype(float).T + dataset[:, :, :, "number_of_memory_accesses_log_read"].astype(float).T) / (dataset[:, :, :, "number_of_memory_accesses"].astype(float).T - dataset[:, :, :, "number_of_memory_accesses_host_dram_hit"].astype(float).T)
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "write_operation"))[::-1]
    with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)








elif "op_sensitivity_write_log_write_ratio" in target_dir:

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "log_size",
        "measurements"
    ])

    # workloads.pop("bfs-dense-8")
    sparser.reregister(list(workloads.keys()), 1)
    sparser.register(r"main(\d+)g-([\d_]+)-w([\d_]+)", regex=True)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file, ndim=2, persist=True)
        if len(remaining) == 0:
            sparser.clear_matches()
            if this_setting is None or this_workload is None:
                continue
            log_size = "64MB"

        else:
            remaining, (this_setting, this_workload, this_log_size), (_, _, aux_log_size) = sparser.recognize(remaining)
            if len(remaining) > 0 or this_setting is None or this_workload is None or aux_log_size is None:
                continue

            dram_size_info = [float(x.replace("_", ".")) for x in aux_log_size]
            if not np.isclose(dram_size_info[0], 2) or not np.isclose(dram_size_info[1], 0.25):
                continue
            log_size = f"{round(dram_size_info[0] * dram_size_info[1] * dram_size_info[2] * 1024)}MB"
            if log_size == "0MB":
                log_size = "0.5MB"

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]
        if this_setting != "SkyByte-WP":
            continue

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, log_size, k], v)

    dataset.set_sort_rules("log_size",
            lambda arr: sorted(arr, key=lambda log_size: [
            rmatch := re.search(r"^[^\d]*(\d*\.?\d+)", log_size),
            0 if rmatch is None else (float(rmatch.group(0)) if len(rmatch.groups()) > 0 else 0)
        ][-1])
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    with open("sen.log.dat", "w") as fout:
        if verbose:
            dataset.dump(fout, indent=2)
        else:
            dataset.dump(fout)

    del dataset

    with open("sen.log.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    result = dataset[:, :, :, "write_operation"].astype(float).T / dataset[:, :, :, "number_of_memory_accesses"].astype(float).T
    np.set_printoptions(linewidth=200, formatter={'float_kind':'{:20f}'.format})
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "write_operation"))[::-1]
    print(result_names)
    print(result)
    with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    result = dataset[:, :, :, "write_operation"].astype(float).T / dataset[:, :, :, "number_of_memory_accesses"].astype(float).T
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "write_operation"))[::-1]
    with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)





        
elif "t_sensitivity_write_log_w_traffic" in target_dir:

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "log_size",
        "measurements"
    ])

    # workloads.pop("bfs-dense-8")
    sparser.reregister(list(workloads.keys()), 1)
    sparser.register(r"main(\d+)g-([\d_]+)-w([\d_]+)", regex=True)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file, ndim=2, persist=True)
        if len(remaining) == 0:
            sparser.clear_matches()
            if this_setting is None or this_workload is None:
                continue
            log_size = "64MB"

        else:
            remaining, (this_setting, this_workload, this_log_size), (_, _, aux_log_size) = sparser.recognize(remaining)
            if len(remaining) > 0 or this_setting is None or this_workload is None or aux_log_size is None:
                continue

            dram_size_info = [float(x.replace("_", ".")) for x in aux_log_size]
            if not np.isclose(dram_size_info[0], 2) or not np.isclose(dram_size_info[1], 0.25):
                continue
            log_size = f"{round(dram_size_info[0] * dram_size_info[1] * dram_size_info[2] * 1024)}MB"
            if log_size == "0MB":
                log_size = "0.5MB"

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]
        if this_setting != "SkyByte-WP":
            continue

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, log_size, k], v)

    dataset.set_sort_rules("log_size",
            lambda arr: sorted(arr, key=lambda log_size: [
            rmatch := re.search(r"^[^\d]*(\d*\.?\d+)", log_size),
            0 if rmatch is None else (float(rmatch.group(0)) if len(rmatch.groups()) > 0 else 0)
        ][-1])
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    with open("sen.log.dat", "w") as fout:
        if verbose:
            dataset.dump(fout, indent=2)
        else:
            dataset.dump(fout)

    del dataset

    with open("sen.log.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    result = dataset[:, :, :, "total_nand_wr_internal"].astype(float).T
    np.set_printoptions(linewidth=200, formatter={'float_kind':'{:20f}'.format})
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "total_nand_wr_internal"))[::-1]
    print(result_names)
    print(result)
    with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    result = dataset[:, :, :, "total_nand_wr_internal"].astype(float).T
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "total_nand_wr_internal"))[::-1]
    with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)





elif "sensitivity_write_log" in target_dir:

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "log_size",
        "measurements"
    ])

    # workloads.pop("bfs-dense-8")
    sparser.reregister(list(workloads.keys()), 1)
    sparser.register(r"main(\d+)g-([\d_]+)-w([\d_]+)", regex=True)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file, ndim=2, persist=True)
        if len(remaining) == 0:
            sparser.clear_matches()
            if this_setting is None or this_workload is None:
                continue
            log_size = "64MB"

        else:
            remaining, (this_setting, this_workload, this_log_size), (_, _, aux_log_size) = sparser.recognize(remaining)
            if len(remaining) > 0 or this_setting is None or this_workload is None or aux_log_size is None:
                continue

            dram_size_info = [float(x.replace("_", ".")) for x in aux_log_size]
            if not np.isclose(dram_size_info[0], 2) or not np.isclose(dram_size_info[1], 0.25):
                continue
            log_size = f"{round(dram_size_info[0] * dram_size_info[1] * dram_size_info[2] * 1024)}MB"
            if log_size == "0MB":
                log_size = "0.5MB"

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]
        if this_setting != "SkyByte-WP":
            continue

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, log_size, k], v)

    dataset.set_sort_rules("log_size",
            lambda arr: sorted(arr, key=lambda log_size: [
            rmatch := re.search(r"^[^\d]*(\d*\.?\d+)", log_size),
            0 if rmatch is None else (float(rmatch.group(0)) if len(rmatch.groups()) > 0 else 0)
        ][-1])
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    with open("sen.log.dat", "w") as fout:
        if verbose:
            dataset.dump(fout, indent=2)
        else:
            dataset.dump(fout)

    del dataset

    with open("sen.log.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    result = dataset[:, :, :, "core_exe_time_avg"].astype(float).T
    np.set_printoptions(linewidth=200, formatter={'float_kind':'{:20f}'.format})
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "core_exe_time_avg"))[::-1]
    print(result_names)
    print(result)
    with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    result = dataset[:, :, :, "core_exe_time_max"].astype(float).T
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "core_exe_time_max"))[::-1]
    with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)
        

elif "bandwidth_nthreads" in target_dir:

    with open("all.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    result = dataset["SkyByte-WP", :, "number_of_memory_accesses_ssd_cache_miss"] / dataset["SkyByte-WP", :, "number_of_memory_accesses"].astype(float)
    result_names = dataset.get_setting_name_slice_arr(("SkyByte-WP", ...))
    print(result)
    # with open(os.path.join(target_dir, "mem_request_nr.dat"), "w") as fout:
    #     ldg.dump(result_names, result, fout, verbose=True)

    # generate overlay line plot
    with open("all.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    n_mem_access = dataset.get_data_slice(
        [None, None, "number_of_memory_accesses"]).astype(float)
    n_host_hit = dataset.get_data_slice(
        [None, None, "number_of_memory_accesses_host_dram_hit"]).astype(float)
    n_log_read = dataset.get_data_slice(
        [None, None, "number_of_memory_accesses_log_read"]).astype(float)
    n_log_write = dataset.get_data_slice(
        [None, None, "number_of_memory_accesses_log_write"]).astype(float)
    n_ssd_hit = dataset.get_data_slice(
        [None, None, "number_of_memory_accesses_ssd_cache_hit"]).astype(float)
    n_ssd_miss = dataset.get_data_slice(
        [None, None, "number_of_memory_accesses_ssd_cache_miss"]).astype(float)

    host_hit_latency = dataset.get_data_slice(
        [None, None, "avg_host_hit_latency"]).astype(float)
    log_read_latency = dataset.get_data_slice(
        [None, None, "avg_log_read_latency"]).astype(float)
    log_write_latency = dataset.get_data_slice(
        [None, None, "avg_log_write_latency"]).astype(float)
    cache_hit_latency = dataset.get_data_slice(
        [None, None, "avg_cache_hit_latency"]).astype(float)
    cache_miss_latency = dataset.get_data_slice(
        [None, None, "avg_cache_miss_latency"]).astype(float)
    total_latency = dataset.get_data_slice(
        [None, None, "overall_average_latency"]).astype(float)

    result_names = dataset.get_setting_name_slice([None, None, "total_nand_wr_user"])
    flatflash_idx = result_names[0].index("SkyByte-P")
    basetype3_idx = result_names[0].index("Base-CSSD")
    # group_str_idx = ["bc", "dlrm", "radix"]
    group_str_idx = []
    group_int_idx = [result_names[1].index(str_idx) for str_idx in group_str_idx]

    # save for later use
    all_total_latency = total_latency * n_mem_access

    WP_total_mem_access = dataset["SkyByte-WP", group_str_idx, "number_of_memory_accesses"].astype(float)
    flatflash_total_mem_access = dataset["SkyByte-P", group_str_idx, "number_of_memory_accesses"].astype(float)
    basetype3_total_mem_access = dataset["Base-CSSD", group_str_idx, "number_of_memory_accesses"].astype(float)
    n_host_hit[flatflash_idx, group_int_idx] -= (flatflash_total_mem_access - WP_total_mem_access)
    n_ssd_hit[basetype3_idx, group_int_idx] -= (basetype3_total_mem_access - WP_total_mem_access)
    n_mem_access[flatflash_idx, group_int_idx] -= (flatflash_total_mem_access - WP_total_mem_access)
    n_mem_access[basetype3_idx, group_int_idx] -= (basetype3_total_mem_access - WP_total_mem_access)

    cxl_latency = 40
    ssd_dram_latency = 46
    indexing_latency = 72

    all_total_latency[flatflash_idx, group_int_idx] -= (flatflash_total_mem_access - WP_total_mem_access) * host_hit_latency[flatflash_idx, group_int_idx]
    all_total_latency[basetype3_idx, group_int_idx] -= (basetype3_total_mem_access - WP_total_mem_access) * (cxl_latency + indexing_latency + ssd_dram_latency)

    total_latency = all_total_latency / n_mem_access
    host_dram = n_host_hit * host_hit_latency
    cxl_protocol = (n_log_read + n_log_write + n_ssd_hit + n_ssd_miss) * cxl_latency
    indexing = (n_log_read + n_ssd_hit + n_ssd_miss) * indexing_latency
    ssd_dram = (n_log_read + n_log_write + n_ssd_hit + n_ssd_miss) * ssd_dram_latency
    # flash_latency = n_ssd_miss * (cache_miss_latency - cxl_latency - indexing_latency - ssd_dram_latency)
    flash_latency = total_latency * n_mem_access - host_dram - cxl_protocol - indexing - ssd_dram
    total = host_dram + cxl_protocol + indexing + ssd_dram + flash_latency

    result_names = dataset.get_setting_name_slice_arr((..., ...))
    result = flash_latency[result_names[0].index("SkyByte-WP"), :] / n_ssd_miss[result_names[0].index("SkyByte-WP"), :]
    result_names = [result_names[1]]
    print(result_names)
    print(result)
    with open(os.path.join(target_dir, "cache_miss_lat.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

elif "bandwidth_ncores" in target_dir:

    dataset_base = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "measurements"
    ])

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None:
            continue

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]
        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset_base.insert_data([this_setting, this_workload, k], v)

    dataset_base.set_sort_rules("settings",
        lambda arr: sorted(
            arr,
            key=lambda setting: list(settings.values()).index(setting)
        )
    )
    dataset_base.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset_base.sort()

    settings = {
        "assd-Full-rr" : "SkyByte-Full",
    }

    workloads = {
        "bc"        : "bc",
        "bfs-dense" : "bfs-dense",
        "dlrm"      : "dlrm",
        "radix"     : "radix",
        "srad"      : "srad",
        "tpcc"      : "tpcc",
        "ycsb"      : "ycsb",
    }

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "measurements"
    ])

    sparser.reregister(r"(\d+)-assd-Full-rr", 0, True)
    sparser.reregister(list(workloads.keys()), 1)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), (nthreads, _) = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None:
            continue

        assert len(nthreads) > 0
        this_setting = nthreads[0]
        this_workload = workloads[this_workload]
        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, k], v)

    dataset.set_sort_rules("settings",
        lambda arr: sorted(
            arr,
            key=lambda setting: int(setting)
        )
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    wp_mem_accesses = dataset_base["SkyByte-WP", :, "number_of_memory_accesses"].astype(float)

    result = (wp_mem_accesses - dataset[:, :, "number_of_memory_accesses_host_dram_hit"].astype(float)) * 64 / (dataset[:, :, "core_exe_time_avg"].astype(float) / 4)
    result_names = dataset.get_setting_name_slice_arr((..., ...))
    with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

elif "nthreads" in target_dir:

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "measurements"
    ])

    settings = {
        "assd-WP" : "SkyByte-WP",
    }

    workloads = {
        "bc-8"        : "bc",
        "bfs-dense-8" : "bfs-dense",
        "dlrm-8"      : "dlrm",
        "radix-8"     : "radix",
        "srad-8"      : "srad",
        "tpcc-8"      : "tpcc",
        "ycsb-8"      : "ycsb-a",
        "ycsbB-8"     : "ycsb-b",
    }

    sparser.reregister(list(settings.keys()), 0)
    sparser.reregister(list(workloads.keys()), 1)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None:
            continue
        this_setting = "8"
        this_workload = workloads[this_workload]

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, k], v)

    settings = {
        "assd-Full-rr" : "SkyByte-Full",
    }

    workloads = {
        "bc"        : "bc",
        "bfs-dense" : "bfs-dense",
        "dlrm"      : "dlrm",
        "radix"     : "radix",
        "srad"      : "srad",
        "tpcc"      : "tpcc",
        "ycsb"      : "ycsb-a",
        "ycsbB"     : "ycsb-b",
    }

    sparser.reregister(r"(\d+)-assd-Full-rr", 0, True)
    sparser.reregister(list(workloads.keys()), 1)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), (nthreads, _) = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None:
            continue

        assert len(nthreads) > 0
        this_setting = nthreads[0]
        this_workload = workloads[this_workload]
        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, k], v)

    dataset.set_sort_rules("settings",
        lambda arr: sorted(
            arr,
            key=lambda setting: int(setting)
        )
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    result = dataset[:, :, "core_exe_time_avg"].astype(float)
    result_names = dataset.get_setting_name_slice_arr((..., ..., "core_exe_time_avg"))
    with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    result = dataset[:, :, "core_exe_time_max"].astype(float)
    result_names = dataset.get_setting_name_slice_arr((..., ..., "core_exe_time_max"))
    with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    # nthread overlay, total bandwidth
    with open("all.dat", "r") as fin:
        dataset_base = mdr.NamedArray.from_input_file(fin)

    wp_mem_accesses = dataset_base["SkyByte-WP", :, "number_of_memory_accesses"].astype(float)
    ssd_bw = (wp_mem_accesses - dataset[:, :, "number_of_memory_accesses_host_dram_hit"].astype(float)) * 64 / (dataset[:, :, "core_exe_time_max"].astype(float) / 4)
    result_names = dataset.get_setting_name_slice_arr((..., ...))
    host_mem_bw = dataset[:, :, "number_of_memory_accesses_host_dram_hit"].astype(float) * 64 / (dataset[:, :, "core_exe_time_max"].astype(float) / 4)
    result = np.stack((ssd_bw, host_mem_bw), axis=2)
    result_names.append(["SSD Bandwidth", "DRAM Bandwidth"])
    with open(os.path.join(target_dir, "avg_bandwidth_split.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)
    with open(os.path.join(target_dir, "avg_bandwidth.dat"), "w") as fout:
        ldg.dump(result_names[:-1], ssd_bw + host_mem_bw, fout, verbose=True)

    # result = dataset[:, :, "core_exe_time_max"].astype(float)
    # result_names = dataset.get_setting_name_slice_arr((..., ..., "core_exe_time_max"))
    # with open(os.path.join(target_dir, "max.dat"), "w") as fout:
    #     ldg.dump(result_names, result, fout, verbose=True)
    
    
elif "flen_nthreads" in target_dir:

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "measurements"
    ])

    settings = {
        "assd-WP" : "SkyByte-WP",
    }

    workloads = {
        "bc-8"        : "bc",
        "bfs-dense-8" : "bfs-dense",
        "dlrm-8"      : "dlrm",
        "radix-8"     : "radix",
        "srad-8"      : "srad",
        "tpcc-8"      : "tpcc",
        "ycsb-8"      : "ycsb-a",
        "ycsbB-8"     : "ycsb-b",
    }

    sparser.reregister(list(settings.keys()), 0)
    sparser.reregister(list(workloads.keys()), 1)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None:
            continue
        this_setting = "8"
        this_workload = workloads[this_workload]

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, k], v)

    settings = {
        "assd-Full-rr" : "SkyByte-Full",
    }

    workloads = {
        "bc"        : "bc",
        "bfs-dense" : "bfs-dense",
        "dlrm"      : "dlrm",
        "radix"     : "radix",
        "srad"      : "srad",
        "tpcc"      : "tpcc",
        "ycsb"      : "ycsb-a",
        "ycsbB"     : "ycsb-b",
    }

    sparser.reregister(r"(\d+)-assd-Full-rr", 0, True)
    sparser.reregister(list(workloads.keys()), 1)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), (nthreads, _) = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None:
            continue

        assert len(nthreads) > 0
        this_setting = nthreads[0]
        this_workload = workloads[this_workload]
        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, k], v)

    dataset.set_sort_rules("settings",
        lambda arr: sorted(
            arr,
            key=lambda setting: int(setting)
        )
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    result = dataset[:, :, "avg_flash_read_latency"].astype(float)
    result_names = dataset.get_setting_name_slice_arr((..., ..., "avg_flash_read_latency"))
    with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    result = dataset[:, :, "avg_flash_read_latency"].astype(float)
    result_names = dataset.get_setting_name_slice_arr((..., ..., "avg_flash_read_latency"))
    with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    # nthread overlay, total bandwidth
    with open("all.dat", "r") as fin:
        dataset_base = mdr.NamedArray.from_input_file(fin)

    wp_mem_accesses = dataset_base["SkyByte-WP", :, "number_of_memory_accesses"].astype(float)
    ssd_bw = (wp_mem_accesses - dataset[:, :, "number_of_memory_accesses_host_dram_hit"].astype(float)) * 64 / (dataset[:, :, "core_exe_time_max"].astype(float) / 4)
    result_names = dataset.get_setting_name_slice_arr((..., ...))
    host_mem_bw = dataset[:, :, "number_of_memory_accesses_host_dram_hit"].astype(float) * 64 / (dataset[:, :, "core_exe_time_max"].astype(float) / 4)
    result = np.stack((ssd_bw, host_mem_bw), axis=2)
    result_names.append(["SSD Bandwidth", "DRAM Bandwidth"])
    with open(os.path.join(target_dir, "avg_bandwidth_split.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)
    with open(os.path.join(target_dir, "avg_bandwidth.dat"), "w") as fout:
        ldg.dump(result_names[:-1], ssd_bw + host_mem_bw, fout, verbose=True)

    # result = dataset[:, :, "core_exe_time_max"].astype(float)
    # result_names = dataset.get_setting_name_slice_arr((..., ..., "core_exe_time_max"))
    # with open(os.path.join(target_dir, "max.dat"), "w") as fout:
    #     ldg.dump(result_names, result, fout, verbose=True)

elif "page_locality_write" in target_dir:

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "rw",
        "modification_ratio"
    ])

    [settings.pop(k) for k in [k for k in settings if settings[k] == "SkyByte-P"]]
    sparser.reregister(list(settings.keys()), 0)

    # [workloads.pop(k) for k in [k for k in workloads if workloads[k] in ["bfs-dense", "srad"]]]
    # sparser.reregister(list(workloads.keys()), 1)

    # hardcode this, this is garbage
    output_dir_alt = os.path.join(base_dir, "output-mar29")
    for stat_file in os.listdir(output_dir_alt):
        # hardcode this, this is garbage
        end_with = "4-baseType3-main2g-0_25-w0_25_parsed_W_amp.txt"
        if not stat_file.endswith(end_with):
            continue

        this_setting = "baseType3"
        # hardcode this, do not ask me what is this
        this_workload = f"{stat_file[:-len(end_with) - 1]}-8"
        if this_workload not in workloads.keys():
            continue

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]

        stats = {}
        with open(os.path.join(output_dir_alt, stat_file), "r") as fin:
            for line in fin:
                if len(line.strip()) == 0:
                    continue
                split = [s.strip() for s in line.split(":")]
                w_modified_ratio = f"{float(split[0])}"
                w_modified_count = int(split[1])
                stats[w_modified_ratio] = w_modified_count
        total_count = sum(stats.values())
        for k, v in stats.items():
            dataset.insert_data([this_setting, this_workload, "write", k], v / total_count if total_count != 0 else np.nan)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), _ = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None:
            continue

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]
        if this_setting == "BaseType3":
            continue

        stat_parser = sp.StatParser(parse_line, parse_final_data, {"rw_mod": False})
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in ((k, v) for k, v in stat_parser.get_dataset().items() if "_modified_" in k):
            split = k.split("_")
            # this is WRITE, thanks to garbage code written by me, read and write is flipped
            rw = "read" if split[0] != "r" else "write"
            ratio = split[-1]
            dataset.insert_data([this_setting, this_workload, rw, ratio], v)

    dataset.set_sort_rules("settings",
        lambda arr: sorted(
            arr,
            key=lambda setting: list(settings.values()).index(setting)
        )
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.sort()

    result = dataset[:, :, "write", :].astype(float)
    result_names = dataset.get_setting_name_slice_arr((..., ..., "write", ...))

    # print(result, result_names)
    for i in range(1, result.shape[-1]):
        result[:, :, i] += result[:, :, i - 1]

    with open(os.path.join(target_dir, "cdf.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

elif "ctx_swtich_threshold" in target_dir:

    settings = {
        "assd-Full-rr" : "SkyByte-Full"
    }

    workloads = {
        "bc-8"         : "bc",
        "bc-16"        : "bc",
        "bc-24"        : "bc",
        "bfs-dense-8"  : "bfs-dense",
        "bfs-dense-16" : "bfs-dense",
        "bfs-dense-24" : "bfs-dense",
        "dlrm-8"       : "dlrm",
        "dlrm-16"      : "dlrm",
        "dlrm-24"      : "dlrm",
        "radix-8"      : "radix",
        "radix-24"     : "radix",
        "radix-48"     : "radix",
        "srad-8"       : "srad",
        "srad-16"      : "srad",
        "srad-40"      : "srad",
        "tpcc-8"       : "tpcc",
        "tpcc-16"      : "tpcc",
        "tpcc-24"      : "tpcc",
        "ycsb-8"       : "ycsb",
        "ycsb-16"      : "ycsb",
        "ycsb-24"      : "ycsb",
    }

    sparser.reregister(list(settings.keys()), 0, regex=False)
    sparser.reregister(list(workloads.keys()), 1, regex=False)
    sparser.register(r"cst(\d+)", regex=True)

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "context_switch_time",
        "measurements"
    ])

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload, _), (_, _, this_cst_time) = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None or this_cst_time is None:
            continue

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]
        this_cst_time = this_cst_time[0]

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, this_cst_time, k], v)

    dataset.set_sort_rules("settings",
        lambda arr: sorted(
            arr,
            key=lambda setting: list(settings.values()).index(setting)
        )
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.set_sort_rules("context_switch_time",
        lambda arr: sorted(
            arr,
            key=lambda context_switch_time: int(context_switch_time)
        )
    )
    dataset.sort()

    with open("cst.dat", "w") as fout:
        if verbose:
            dataset.dump(fout, indent=2)
        else:
            dataset.dump(fout)

    del dataset

    with open("cst.dat", "r") as fin:
        dataset = mdr.NamedArray.from_input_file(fin)

    result = dataset[:, :, :, "core_exe_time_max"].astype(float)
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "core_exe_time_avg"))
    result_names.insert(0, list(settings.values()))
    with open(os.path.join(target_dir, "avg.dat"), "w") as fout:
        ldg.dump(result_names, np.expand_dims(result, 0), fout, verbose=True)

    result = dataset[:, :, :, "core_exe_time_max"].astype(float).T
    result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "core_exe_time_max"))[::-1]
    with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

elif "sensitivity_nand_latency" in target_dir:

    dataset = mdr.NamedArray.from_dim_names([
        "settings",
        "workloads",
        "flash_type",
        "measurements"
    ])

    workloads = {
        "bc"        : "bc",
        "bfs-dense" : "bfs-dense",
        "dlrm"      : "dlrm",
        "radix"     : "radix",
        "srad"      : "srad",
        "tpcc"      : "tpcc",
        "ycsb"      : "ycsb-a",
        "ycsbB"     : "ycsb-b",
    }

    flash_types = {
        "flash0" : "ULL",
        "flash3" : "ULL2",
        "flash1" : "SLC",
        "flash2" : "MLC",
    }

    sparser.reregister(r"(\d+)-assd-Full-rr", 0, True)
    sparser.reregister(list(workloads.keys()), 1)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload), (nthreads, _) = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None:
            continue

        assert len(nthreads) > 0
        if nthreads[0] not in ["32", "16", "24"]:
            continue
        
        this_setting = f"SkyByte-Full-{nthreads[0]}"
        this_workload = workloads[this_workload]
        this_flash = "ULL"
        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, this_flash, k], v)

    sparser.reregister(r"(\d+)-assd-Full-rr", 0, True)
    sparser.reregister(list(workloads.keys()), 1)
    sparser.register(list(flash_types.keys()))

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload, this_flash), (nthreads, _, _) = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None or this_flash is None:
            continue

        assert len(nthreads) > 0
        this_setting = f"SkyByte-Full-{nthreads[0]}"
        this_workload = workloads[this_workload]
        this_flash = flash_types[this_flash]
        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, this_flash, k], v)

    settings = {
        "baseType3" : "Base-CSSD",
        "flatflash" : "SkyByte-P",
        "assd-W"    : "SkyByte-W",
        "assd-WP"   : "SkyByte-WP",
        "assd-C-rr" : "SkyByte-C",
        "assd-Full-rr" : "SkyByte-Full-8",
    }
    workloads = {
        "bc-8"        : "bc",
        "bfs-dense-8" : "bfs-dense",
        "dlrm-8"      : "dlrm",
        "radix-8"     : "radix",
        "srad-8"      : "srad",
        "tpcc-8"      : "tpcc",
        "ycsb-8"      : "ycsb-a",
        "ycsbB-8"     : "ycsb-b",
    }

    sparser.reregister(list(settings.keys()), 0)
    sparser.reregister(list(workloads.keys()), 1)
    sparser.reregister(list(flash_types.keys()), 2)

    for stat_file in os.listdir(output_dir):
        if any([s in stat_file for s in ("txt", "DRAM")]):
            continue
        remaining, (this_setting, this_workload, this_flash), _ = sparser.recognize(stat_file)
        if len(remaining) > 0 or this_setting is None or this_workload is None or this_flash is None:
            continue

        this_setting = settings[this_setting]
        this_workload = workloads[this_workload]
        this_flash = flash_types[this_flash]

        stat_parser = sp.StatParser(parse_line, parse_final_data)
        stat_parser_wrapper = sp.SectionParsers(None)
        stat_parser_wrapper.register_section_parser(stat_parser)

        with open(os.path.join(output_dir, stat_file), "r") as fin:
            stat_parser_wrapper.parse(fin)
        for k, v in stat_parser.get_dataset().items():
            dataset.insert_data([this_setting, this_workload, this_flash, k], v)

    with open("all.dat") as fin:
        dataset_base = mdr.NamedArray.from_input_file(fin)

    dim_vals = dataset_base[:, :, "core_exe_time_max"].astype(float)
    dim_names = dataset_base.get_setting_name_slice_arr((..., ..., "core_exe_time_max"))
    for (setting_idx, workload_idx), v in np.ndenumerate(dim_vals):
        if dim_names[0][setting_idx] in ["Base-CSSD", "SkyByte-Full"]:
            continue
        this_setting = dim_names[0][setting_idx]
        this_workload = dim_names[1][workload_idx]
        this_flash = "ULL"
        dataset.insert_data([this_setting, this_workload, this_flash, "core_exe_time_max"], v)

    print(dataset_base.get_setting_name_slice_arr((..., ..., "core_exe_time_max")))
    dataset.set_sort_rules("settings",
        lambda arr: sorted(
            arr,
            key=lambda setting: [
                *list(settings.values()),
                "SkyByte-Full-16", "SkyByte-Full-24", "SkyByte-Full-32"
            ].index(setting)
        )
    )
    dataset.set_sort_rules("workloads",
        lambda arr: sorted(
            arr,
            key=lambda workload: list(workloads.values()).index(workload)
        )
    )
    dataset.set_sort_rules("flash_type",
        lambda arr: sorted(
            arr,
            key=lambda flash_type: list(flash_types.values()).index(flash_type)
        )
    )
    dataset.sort()

    workloads_selected = ['SkyByte-P', 'SkyByte-W', 'SkyByte-WP', 'SkyByte-Full-16', 'SkyByte-Full-24', 'SkyByte-Full-32']
    result = dataset[workloads_selected, :, :, "core_exe_time_max"].astype(float)
    result_names = dataset.get_setting_name_slice_arr((workloads_selected, ..., ..., "core_exe_time_max"))
    print(result_names)
    with open(os.path.join(target_dir, "max.dat"), "w") as fout:
        ldg.dump(result_names, result, fout, verbose=True)

    # result = dataset[:, :, :, "core_exe_time_max"].astype(float).T
    # result_names = dataset.get_setting_name_slice_arr((..., ..., ..., "core_exe_time_max"))[::-1]
    # with open(os.path.join(target_dir, "max.dat"), "w") as fout:
    #     ldg.dump(result_names, result, fout, verbose=True)
