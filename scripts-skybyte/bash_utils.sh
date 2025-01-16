#!/bin/bash

display_yesno_option() {
  read -rp "(Y/[n]) ? " selection
  return $(test "$selection" == "Y")
}

nice_sleep() {
  wait_arr=( "\u28BF" "\u28FB" "\u28FD" "\u28FE" "\u28F7" "\u28EF" "\u28DF" "\u287F" )
  i=0
  background=1
  sleep_duration=0.05
  sleep_iter=$(echo "scale=0; $1 / $sleep_duration" | bc)
  while [ $i -le "$sleep_iter" ]; do
    case $(ps -o stat= -p $$) in
      *+*)
        if [ $background -eq 1 ]; then
          background=0
          tput sc
        fi
        print_idx=$(( i % ${#wait_arr[@]} ))
        tput rc
        echo -en "\033[2K${wait_arr[print_idx]} $2" >&2
        ;;
      *)
        background=1
        ;;
    esac
    i=$(( i + 1 ))
    sleep "$sleep_duration"
  done
  echo -en "\r" >&2
  tput el1
}

nice_sleep_clearline() {
  [[ -z $3 ]] && print_output=1 || print_output="$3"
  wait_arr=( "\u28BF" "\u28FB" "\u28FD" "\u28FE" "\u28F7" "\u28EF" "\u28DF" "\u287F" )
  i=0
  background=1
  sleep_duration=0.05
  sleep_iter=$(echo "scale=0; $1 / $sleep_duration" | bc)
  while [ $i -le "$sleep_iter" ]; do
    case $(ps -o stat= -p $$) in
      *+*)
        if [ $background -eq 1 ]; then
          background=0
        fi
        print_idx=$(( i % ${#wait_arr[@]} ))
        echo -en "\r\033[2K${wait_arr[print_idx]} $2" >&"$print_output"
        ;;
      *)
        background=1
        ;;
    esac
    i=$(( i + 1 ))
    sleep "$sleep_duration"
  done
  echo -en "\r" >&"$print_output"
  tput el1
}
