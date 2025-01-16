#!/bin/bash

# This is an automated script to run the experiments and generate key results in the paper.

# --------------------------------- IMPORTART -------------------------------------------------
# ! Please modify this number based on your machine's main memory capacity. One experiment process will need a peak memory of around 13 GB.
# We recommend reserving 15 GB for each process to ensure that the program won't crash.
# For example, if your machine has 128 GB of main memory, this number can be set as 8.
MAX_CORES_NUM=8
# ---------------------------------------------------------------------------------------------

# Prerequisite: 
# ./pre_req.sh


python3 build.py -c
python3 build.py macsim.config -j "$(nproc)"
cd scripts-skybyte


#-------------------------------- Running Experiments -----------------------------------------------------------------------------------
# Make sure there is no other experiment running in the background (in tmux)
tmux kill-server

# Setup experiment configurations for figure 2, 3, 4, 14, 15, 16, 17, 18, and Table 3
./run_full.sh
# After running this, you will see a folder named bin-<workload_name>-<thread_num>-<baseline_name> for each experiment
# In each folder, there is a script named run_one.sh, which is used to run the experiment

# Clean up the previous pipe logs (not important for reviewers)
rm *.pipe

# Run experiments for figure figure 2, 3, 4, 14, 15, 16, 17, 18, and Table 3 concurrently with multiple cores
./run_all.sh -p "bc|tpcc|srad|radix|ycsb|dlrm|bfs-dense" -dr -j $MAX_CORES_NUM
# The time for running this could be long

