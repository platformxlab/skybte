# Scripts for SkyByte Artifact

This directory contains a series of scripts for the user to conveniently run the experiments and generate results. 

### run_full.sh

This shell script is used to setup all experiemnts needed for reproducing key results of the paper: figure 2, 3, 4, 14, 15, 16, 17, 18, and Table 3.

After running this, you will see multile folders named `bin-<workload_name>-<thread_num>-<baseline_name>` for different experiments. In each folder, there is a script named run_one.sh, which is used to run the specific experiment.



### run_all.sh
This shell script is used to conveniently run multiple experiments concurrently. It supports using regular expressions to match multiple config files, and it will automatically spawn different experiments to multiple `tmux` windows for parallel execution.

Usage:
```bash
Run simulation
  -h             print help, this message
  -g             regenerate all configs from genconfigs.py in configs folder
  -d             rerun all invalid output files
  -r             run all configs that does not have output
  -k             remove all output, comfirmation required
  -t [TTY]       progress tty
  -j [NUM_PROC]  max number of CPU cores used for simulation
  -p [REGEX]     match specific config pattern
  -dr            rerun all configs that either invalid or not generated
Return values
  0              script terminates correctly
  1              invalid options
  2              abort on removal of critical files
  3              abort on simulation launching
  4              abort on required resources invalid/missing
```

Examples of using this script can be found in `../artifact_run.sh`. When you batch a number of experiments, you can use `tmux ls` to see all the spawned tmux sessions. You can also use `htop` to see the processes' information.

