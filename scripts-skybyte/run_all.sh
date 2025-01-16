#!/bin/bash

trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM

print_help() {
  printf "Useage: %s:\n" "$1" >&2
  printf "Run simulation\n" >&2
  printf "  -h             print help, this message\n" >&2
  printf "  -g             regenerate all configs from genconfigs.py in configs folder\n" >&2
  printf "  -d             rerun all invalid output files\n" >&2
  printf "  -r             run all configs that does not have output\n" >&2
  printf "  -f             run all configs that have valid output to regenerate final stat (Not Implemented)\n" >&2
  printf "  -k             remove all output, comfirmation required\n" >&2
  printf "  -t [TTY]       progress tty\n" >&2
  printf "  -j [NUM_PROC]  max number of CPU cores used for simulation\n" >&2
  printf "  -p [REGEX]     match specific config pattern\n" >&2
  printf "  -dr            rerun all configs that either invalid or not generated\n" >&2
  printf "Return values\n" >&2
  printf "  0              script terminates correctly\n" >&2
  printf "  1              invalid options\n" >&2
  printf "  2              abort on removal of critical files\n" >&2
  printf "  3              abort on simulation launching\n" >&2
  printf "  4              abort on required resources invalid/missing\n" >&2
}

# TODO:  integrate color removal to output log
# sed -i -r "s/\x1B\[([0-9]{1,3}(;[0-9]{1,2};?)?)?[mGK]//g" [filename]

# abs path to base folder
base_folder="$(dirname "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)")"
scripts_folder="$base_folder/scripts-skybyte"
output_folder="$base_folder/output"

source "$scripts_folder/bash_utils.sh"

# options to be set
RERUN_FAILED=0
RUN_VALID=0
REMOVE_INVALID=0
RM_EVERYTHING=0
MAX_CONCURRENT_RUN=8

# W             3
# WP            4
# baseType3     2
# flatflash(P)  3

APPLICATION="macsim"
GEN_CONFIG="$scripts_folder/run_sen_1.sh"
INPUT_SETTINGS_FILE="$scripts_folder/print.txt"
PATTERN=".*"
STATUS_TTY="/dev/null"
# parse command line args
while getopts "hgdrfkt:p:j:" arg; do
  case $arg in
    h)
      print_help "$@"
      exit 0
    ;;
    g)
      if ! [ -f "$GEN_CONFIG" ]; then
        printf "\033[0;31mConfig generation script <%s> not found\033[0m\n" "$GEN_CONFIG" >&2
        exit 4
      fi
      if [ -f "$INPUT_SETTINGS_FILE" ]; then
        IFS=$'\n' readarray -t input_folders < "$INPUT_SETTINGS_FILE"
        overwrite_folders=()
        for folder in "${input_folders[@]}"; do
          folder="$base_folder/$folder"
          if [ -d "$folder" ]; then
            overwrite_folders+=("$folder")
          fi
        done
        if [ "${#overwrite_folders[@]}" -ne 0 ]; then
          printf "Overwritting configs\n" >&2
          printf "  %s\n" "${overwrite_folders[@]}"
          printf "Above folders will be removed, continue " >&2
          display_yesno_option
          if [ $? -ne 0 ]; then
            exit 2
          fi
          printf "%s\n" "${overwrite_folders[@]}" | xargs rm -r
          printf "All configs removed, generating configs\n" >&2
        fi
      fi
      bash "$GEN_CONFIG"
      printf "Config generation done\n" >&2
    ;;
    d)
      process_cnt=$(pgrep $APPLICATION -u "$USER" | wc -l)
      if [[ $process_cnt -gt 0 ]]; then
        printf "\033[0;31mThere are %d %s running, force clean\033[0m " "$process_cnt" "$APPLICATION" >&2
        display_yesno_option
        if [ $? -eq 0 ]; then
          REMOVE_INVALID=1
        else
          printf "Abort\n" >&2
          exit 3
        fi
      else
        REMOVE_INVALID=1
      fi
    ;;
    r)
      RERUN_FAILED=1
    ;;
    j)
      MAX_CONCURRENT_RUN=${OPTARG}
      re='^[0-9]+$'
      if ! [[ $MAX_CONCURRENT_RUN =~ $re ]]; then
        printf "MAX_CONCURRENT_RUN specified <%s> is not a number\n" "$MAX_CONCURRENT_RUN" >&2
        exit 1
      fi
    ;;
    f)
      RUN_VALID=1
    ;;
    k)
      RM_EVERYTHING=1
    ;;
    t)
      STATUS_TTY=${OPTARG}
    ;;
    p)
      PATTERN=${OPTARG}
      printf "PATTERN: %s\n" "$PATTERN"
    ;;
    *)
      print_help "$@"
      exit 1
    ;;
  esac
done

# some variables
sim_configs=()

IFS=$'\n' readarray -t all_configs < "$INPUT_SETTINGS_FILE"
for file in "${all_configs[@]}"; do
  if ! [[ $file =~ $PATTERN ]]; then continue; fi
  sim_configs+=("$base_folder/$file")
done

printf "Total %d sim configs found\n" "${#sim_configs[@]}" >&2

run_configs=()
delete_configs=()
for target_folder in "${sim_configs[@]}"; do
  target_name="$(basename -- "$target_folder")"
  output_name="$output_folder/${target_name#bin-}"
  if [ -f "$output_name" ]; then
    output_filesize="$(stat --printf="%s" $output_name)"
    if [[ $RM_EVERYTHING -ne 0 ]]; then
      delete_configs+=("$output_name")
    else
      if [ "$output_filesize" -eq 0 ]; then
        if [[ $REMOVE_INVALID -ne 0 ]]; then
          rm "$output_name"
          rm "${output_name}_parsed_R_amp.txt"
          rm "${output_name}_parsed_W_amp.txt"
          rm "${output_name}_warmup_hint_data.txt"
          run_configs+=("$target_folder");
        fi
      fi
    fi
  else
    run_configs+=("$target_folder")
  fi
done

if [ $RM_EVERYTHING -eq 1 ]; then
  printf "Will operate on\n" >&2
  for config in "${delete_configs[@]}"; do
    printf "%s\n" "$(basename "$config")"
  done
  printf "\033[0;31mREMOVE ALL OUTPUT\033[0m " >&2
  display_yesno_option
  if [ $? -ne 0 ]; then
    printf "Abort\n" >&2
    exit 2
  fi
  for target_folder in "${delete_configs[@]}"; do
    target_name="$(basename -- "$target_folder")"
    output_name="$output_folder/${target_name#bin-}"
    rm -f "$output_name"
    rm -f "${output_name}_parsed_R_amp.txt"
    rm -f "${output_name}_parsed_W_amp.txt"
    rm -f "${output_name}_warmup_hint_data_wlog.txt"
    rm -f "${output_name}_prefill_data.txt"
    rm -f "${output_name}_warmup_hint_dram_system.txt"
  done
  exit 0
fi

# W             3
# WP            4
# baseType3     2
# flatflash(P)  3
declare -A type_corenum_mapping
type_corenum_mapping=(
  ["assd-WP"]=1
  ["assd-Full"]=1
  ["assd-C"]=1
  ["baseType3"]=1
  ["flatflash"]=1
  ["assd-W"]=1
  ["assd-A"]=1
  ["assd-CP"]=1
  ["assd-CT"]=1
  ["DRAM-only"]=1
)

progress_pipe_name="$scripts_folder/progress.pipe"
progress_report() {
  print_status_line=""
  while :; do
    if readarray -t status_lines <<< "$(timeout 0.1 cat "$progress_pipe_name")"; then
      for status_line in "${status_lines[@]}"; do
        if [ -z "$status_line" ]; then continue; fi
        if [ "$status_line" = exit ]; then return; fi
        print_status_line="$status_line"
      done
    fi
    nice_sleep_clearline 1 "$print_status_line" "$STATUS_TTY"
  done
}
if [ -p "$progress_pipe_name" ]; then
  rm "$progress_pipe_name"
fi
mkfifo "$progress_pipe_name"
progress_report_pid=-1

if [[ $RERUN_FAILED -ne 0 ]]; then
  comm_pipe_name="$scripts_folder/comm.pipe"
  if [ -p "$comm_pipe_name" ]; then
    rm "$comm_pipe_name"
  fi
  mkfifo "$comm_pipe_name"

  total_ncores=$MAX_CONCURRENT_RUN
  used_ncores=0

  progress_report &
  echo "Run start" > "$progress_pipe_name"

  sessions=()
  num_runs_spawned=0
  num_runs_finish=0
  num_runs_error=0
  num_runs=0
  printf "Rerun %d sim configs\n" "${#run_configs[@]}" >&2
  for target_folder in "${run_configs[@]}"; do
    config="$(basename -- "$target_folder")"
    ncores=-1
    for key in "${!type_corenum_mapping[@]}"; do
      if [[ $config == *$key* ]]; then
        group_name="$key"
        ncores="${type_corenum_mapping[$key]}"
      fi
    done
    if [ "$ncores" -le 0 ]; then
      printf "config %s type not recognized" "$config"
      exit 1
    fi
    config_name="$config"
    while :; do
      projected_cores=$(( "$used_ncores" + "$ncores" ))
      printf "  [Cores]: Current: %d Requested: %d Projected: %d Total: %d\n" "$used_ncores" "$ncores" "$projected_cores" "$total_ncores" >&2
      [ "$projected_cores" -gt "$((total_ncores))" ] || break
      readarray -t returned_cores < "$comm_pipe_name"
      for returned_ncore in "${returned_cores[@]}"; do
        if [ "$returned_ncore" -le 0 ]; then
          (( num_runs_error = num_runs_error + 1 ))
        else
          (( num_runs_finish = num_runs_finish + 1 ))
        fi
        returned_ncore="${returned_ncore#-}"
        projected_cores=$(( "$used_ncores" - "$returned_ncore" ))
        printf "  [Cores]: Return: %d Current: %d -> %d\n" "$returned_ncore" "$used_ncores" "$projected_cores" >&2
        used_ncores="$projected_cores"
      done
    done
    printf " Spawning config %s\n" "$config"
    new_session=0
    if ! tmux has-session -t "$group_name" &> /dev/null; then
      tmux new-session -d -s "$group_name" -c "$base_folder"
      printf "session %s spawned\n" "$group_name" >&2
      sessions+=("$group_name")
      new_session=1
    fi
    if ! tmux has-session -t "$group_name:$config_name" &> /dev/null; then
      log_file="$target_folder/run.log"
      tmux new-window -t "$group_name" -n "$config_name" -c "$target_folder"
      tmux send-keys -t "$group_name:$config_name" "set -o pipefail" Enter
      tmux send-keys -t "$group_name:$config_name" "if stdbuf --output=L ./run_one.sh | tee $log_file; then 
          echo $ncores > $comm_pipe_name; exit; 
        else
          echo -$ncores > $comm_pipe_name; 
        fi" Enter
      (( num_runs = num_runs + 1 ))
    else
      printf "session %s:%s already exists\n" "$group_name" "$config_name" >&2
    fi
    if [ "$new_session" -ne 0 ]; then 
      tmux kill-window -t "$group_name:$(tmux display-message -p "#{base-index}")"
    fi
    used_ncores="$projected_cores"
    (( num_runs_spawned = num_runs_spawned + 1 ))
    message="Spawned: $num_runs_spawned Finished: $num_runs_finish Error: $num_runs_error"
    timeout 0.5 bash -c "echo $message > $progress_pipe_name"
    printf "  [Run Status]: %s\n" "$message"
  done
  printf "All sim configs launched, waiting for completion" >&2
  while [ "$num_runs" -gt 0 ]; do
    num_runs=$(pgrep $APPLICATION -u "$USER" | wc -l)
    message="Remaining runs: $num_runs"
    timeout 0.5 bash -c "echo $message > $progress_pipe_name"
    nice_sleep 10 "$message"
  done
  printf "\nDone\n" >&2
  rm "$comm_pipe_name"

  echo "exit" > "$progress_pipe_name"
  sleep 1
  rm "$progress_pipe_name"
else
  for target_folder in "${run_configs[@]}"; do
    printf "%s\n" "$target_folder"
  done
fi

printf "All Done\n" >&2
