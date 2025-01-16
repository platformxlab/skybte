# SkyByte: Architecting An Efficient Memory-Semantic CXL-based SSD with OS and Hardware Co-design  (Artifact)

In this artifact, we provide the source code of SkyByte's simulation framework and the necessary instructions to reproduce the key performance results in our paper.


## 0. Hardware and Software Dependencies

This artifact can run on any x86 machine with at least 32 GB of RAM and at least 128 GB of disk space. We highly recommend running the artifact on a workstation with multiple powerful CPU cores and at least 64 GB main memory. The artifact requires a Linux environment (preferably Ubuntu 20.04+) and a compiler that supports the C++11 standard.


## 1. Installation

### 1.1 Downloading the Repository

Use the following command to download the artifact:
```
# To Download from Zenodo
# wget https://zenodo.org/records/14660185/files/SkyByte-Artifact.tar.gz

# To Download from Github
git clone git@github.com:HieronZhang/SkyByte-Artifact.git
```

### 1.2 Installation

Install the following dependencies (You can also run `pre_req.sh`):
```
sudo apt update
sudo apt-get install libboost-all-dev
sudo apt install scons htop
sudo apt upgrade g++
pip3 install matplotlib networkx pandas PyPDF2 gdown scipy
```

Build the simulator for SkyByte:
```
python3 build.py macsim.config -j NUM_THREADS
```

## 2. Experiment Workflow
This section describes the steps to generate and run the necessasry experiments. We strongly recommend referring to `scripts-skybyte/README.md` for detailed explanations of each script used in this section.


### 2.1 Preparing the multi-threaded instruction traces

We prepared the instruction traces captured by Intel's PIN tool for the workloads we used in the paper. Download the traces from google drive:

```bash
wget https://zenodo.org/records/14660185/files/skybyte_new_traces.tar.gz
tar -xvf skybyte_new_traces.tar.gz 
```


After uncompressing, make sure to put the ``skybyte_new_traces`` folder and the codebase (the ``SkyByte-Artifact`` folder) in the same directory. 

```bash
the_outer_directory
├── skybyte_new_traces
├── SkyByte-Artifact
└── ...
```

Each set of traces (e.g., the traces for `bc` with 16 threads), includes a trace configuration file (`trace.txt`) and several raw trace files (`trace_XX.raw` files). The trace file format is consistent with that of the Macsim simulator. See section 3.4 of `doc/macsim.pdf` for more details. 

### 2.2 Configuration Files

The ``configs`` directory contains configuration files tailored for various workloads, design baselines and specific settings (e.g., the context-switch policy). For detailed information about
these files, refer to ``configs/README.md``.

### 2.3 Launching A Single Experiment

After compiling the simulation framework, a symbolic link ``macsim`` will appear in the ``bin`` directory. Within the ``bin`` directory, the file ``trace_file_list`` specifies the location of the instruction trace configuration file (the corresponding `trace.txt`). This artifact include scripts to automate the setup the individual experiments (introduced later).

To launch a single experiment, use the following command:

```
cd bin
./macsim -b ../configs/baselines/XX.config -w ../configs/workloads/XX.config (-t ../configs/settings/XX.config) -c {corenum} -o {terminal} -p -f {outputfile_name} (-d) (-r)
```

where the command line arguments are:
```
-b baseline_setting_config_file_name
-w workload_config_filename
-t additional_setting_config_file_name (optional)
-c number_of_logical_cores_to_simlute
-o terminal_for_printing_warmup_logs (e.g. /dev/pts/6)
-p: print detailed runtime information (optional)
-f output_file_name
-d: run with infinite host DRAM (optional)
-r: output DRAM-only performance results (optional)
```

This command sets up the specified configurations (e.g., which design baseline is used), performs a warmup, and replays the instruction traces on multiple simulated CPU cores and the simulated CXL-SSD. Results will be generated in the `output` directory. 


### 2.4 Launching Batched Experiments

To execute a large number of experiments simultaneously, we provide the `scripts-skybyte/run_all.sh` shell script. This script uses regular expressions to match multiple config files, and automatically spawns experiments in separate ``tmux`` windows for parallel execution. 


For convenience, we also provide the ``artifact_run.sh`` script, which automates the setup and execution of all required experiments. To launch all experiments, simply run:
```
./artifact_run.sh
```

The variable `MAX_CORES_NUM` in this script specifies the maximum allowed number of CPU cores for simulations. Users need to adjust this value based on their own machine's specification before running the script. Please modify this number based on your machine's main memory capacity. One experiment process will need a peak memory of around 13 GB. We recommend reserving 15 GB for each process to ensure that the program won't crash. For example, if your machine has 128 GB of main memory, this number can be set as 8. (See line 5-10 of ``artifact_run.sh``)

The artifact run.sh script performs the following tasks: 1. Creates multiple directories named
``bin-<workload>-<thread_num>-<baseline>`` for different experiments. 2. Sets up the corresponding
trace file list file in each directory. 3. Generates a run one.sh script in each directory to facilitate running individual experiments. 4. Uses the run all.sh script to launch parallel experiments. See lines 21-35 of ``artifact_run.sh``:

```
# Setup experiment configurations for figure 2, 3, 4, 14, 15, 16, 17, 18, and Table 3
./run_full.sh
# After running this, you will see a folder named bin-<workload_name>-<thread_num>-<baseline_name> for each experiment
# In each folder, there is a script named run_one.sh, which is used to run the experiment

# Clean up the previous pipe logs (not important for reviewers)
rm *.pipe

# Run experiments for figure figure 2, 3, 4, 14, 15, 16, 17, 18, and Table 3 concurrently with multiple cores
./run_all.sh -p "bc|tpcc|srad|radix|ycsb|dlrm|bfs-dense" -dr -j $MAX_CORES_NUM
```


## 3. Evaluation and Expected Results

To evaluate the artifact results, simply run:
```
./artifact_draw_figs.sh
```

This script gathers all the results from the `output` folder, and draws all the needed figures sequentially. A detailed description of each command and the output figures' positions is also included in this script.

We provide the expected result data files and figures in the same directory where the figures will be generated. To verify the results, you can compare the generated figures directly with those presented in the paper, or compare the data for each figure with the example results we have provided. Note that our simulator is non-deterministic, so the figures may have slight differences from the ones in the paper.



## 4. Experiment Customization

### 4.1 Custom Simulation Configurations. 

In addition to the provided configurations, users have the flexibility to customize their own configuration files for evaluating experiments. Below is a list of configurable parameters (knobs) that can be adjusted:

1. **promotion_enable**: Enables or disables the adaptive page migration mechanism.
2. **write_log_enable**: Enables or disables the CXL-Aware SSD DRAM management.
3. **device_triggered_ctx_swt**: Enables or disables the coordinated context switch mechanism.
4. **cs_threshold**: Specifies the threshold for triggering the context switch policy (Unit: ns).
5. **ssd_cache_size_byte**: Defines the size of the SSD DRAM cache (Unit: Bytes).
6. **ssd_cache_way**: Defines the associativity of the SSD DRAM cache.
7. **host_dram_size_byte**: Specifies the size of the host's main memory (Unit: Bytes).
8. **t_policy**: Specifies the thread scheduling policy. Options include "RR", "RANDOM", or "FAIRNESS" (CFS).


### 4.2 Capturing Custom Program's Traces

Users can generate custom traces for their own programs on their machines. To assist with this, we include a sub-repository called ``macsim-x86trace`` in the artifact. This sub-repo contains the Intel PIN 3.13 tool and scripts that generate both instruction traces and memory warmup traces, which are required by our simulation framework for a custom application. For detailed instructions on how to generate these traces, refer to ``macsim-x86trace/README.md``.

Please note that PIN 3.13 only runs on Ubuntu 18.04.
