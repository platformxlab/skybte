/*
Copyright (c) <2012>, <Georgia Institute of Technology> All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of conditions
and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or other materials provided
with the distribution.

Neither the name of the <Georgia Institue of Technology> nor the names of its contributors
may be used to endorse or promote products derived from this software without specific prior
written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

/**********************************************************************************************
 * File         : main.cc
 * Author       : HPArch and Haoyang
 * Date         : 3/29/2024
 * SVN          : $Id: main.cc 911 2009-11-20 19:08:10Z kacear $:
 * Description  : main file
 *********************************************************************************************/

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <set>

#include <assert.h>
#include <sched.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "SkyByte-Sim/trace_utils.h"
#include "SkyByte-Sim/ssd_stat.h"
#include "SkyByte-Sim/cpu_scheduler.h"
#include "SkyByte-Sim/utils.h"
#include "SkyByte-Sim/ftl.h"
#include "SkyByte-Sim/simulator_clock.h"


#include "macsim.h"
#include "assert_macros.h"
#include "knob.h"
#include "core.h"
#include "utils.h"
#include "statistics.h"
#include "frontend.h"
#include "process_manager.h"
#include "pref_common.h"
#include "trace_read.h"

#include "debug_macros.h"

#include "all_knobs.h"

using std::string;
using std::ifstream;
using std::ofstream;
using std::stringstream;
using std::getline;
using std::abs;
using std::min;
using std::max;
using std::stoi;
using std::stod;
using std::to_string;
using std::unordered_set;
using std::filesystem::exists;

extern ssd gdev;

//Parameters:
extern bool promotion_enable;
extern bool write_log_enable;
extern bool device_triggered_ctx_swt;
extern long cs_threshold;

extern long ssd_cache_size_byte;
extern int ssd_cache_way;
extern long host_dram_size_byte;

string baseline_config_filename;
string workload_config_filename;
string setting_config_filename;
string workload_name;

double ssd_host_rate = 0;
extern double write_log_ratio;

string btype;
string bench;

FILE *output_file;
std::string main_filename;

extern bool print_timing_model;
extern nand_type n_type;

extern const uint64_t rr_timeslice_nano;
extern const uint64_t ctx_swh_deadtime_nano;

extern Thread_Policy_enum t_policy;

bool use_macsim = true;
bool warmed_up_mode = true;
bool pinatrace_drive = false;
extern sim_clock* the_clock_pt;
unordered_set<uint64_t> prefill_pages;
bool prefill_pass = false;
ThreadScheduler* skybyte_scheduler_pt;
bool dram_only = false;
bool dram_baseline = false;
std::vector<uint64_t> ordered_vector;
uint64_t mark_inst_num = 0;

int64_t cache_marked_num = 0;
int64_t host_marked_num = 0; 
bool need_mark = false;

//TPP implementation only
extern vector<uint64_t> ordered_memory_space;
extern bool tpp_enable;
extern std::deque<uint64_t> LRU_active_list;
extern std::set<uint64_t> LRU_inactive_list;
extern std::set<uint64_t> NUMA_scan_set;

//Astriflash implementation only
extern bool astriflash_enable;

int parameter_validation(param *param) {
  int ret = 0;
  bytefs_assert(param->filename_prefix.size() > 0);
  bytefs_assert(param->ntraces > 0);
  bytefs_assert(param->spawn_first > 0 && param->spawn_first <= param->ntraces);
  for (int file_idx = 0; file_idx < param->ntraces; file_idx++) {
    string cur_filename = param->filename_prefix + to_string(file_idx);
    param->trace_filenames.push_back(cur_filename);
    printf("Filename: %s\n", cur_filename.c_str());
  }
  bytefs_assert(param->trace_filenames.size() > 0);
  bytefs_assert(param->logical_core_num > 0);
  bytefs_assert(param->time_scale > 0);

  // status output redirection
  if (param->status_tty_name.size() > 0) {
    ifstream tty_check(param->status_tty_name);
    if (tty_check.good()) {
      param->status_tty = fopen(param->status_tty_name.c_str(), "w");
      if (param->status_tty) {
        bytefs_log("Redirecting status output to tty %s", param->status_tty_name.c_str());
        fprintf(param->status_tty, "%sStat redirection here [%s]\n", 
            ansi_clearscreen, param->status_tty_name.c_str());
      } else {
        param->status_tty = stdout;
        bytefs_err("Error opening output tty %s, fallback to stdout", param->status_tty_name.c_str());
      }
    } else {
      param->status_tty = stdout;
      bytefs_warn("Output tty %s not found, fallback to stdout", param->status_tty_name.c_str());
    }
  }
  return ret;
}


std::string extractFilename(const std::string& path) {
    // Find the position of the last directory separator
    size_t lastSlash = path.find_last_of('/');
    
    // Extract the substring starting from the position after the last slash
    if (lastSlash != std::string::npos && lastSlash < path.length() - 1) {
        return path.substr(lastSlash + 1);
    }
    
    // Return the original path if no directory separator is found
    return path;
}

// input parameter parsing
  param param;


int main(int argc, char** argv) {

  const_init();
  
  print_timing_model = false;
  n_type = nand_type::HLL_NAND;

  bool context_sw_buffer;

  // param.logical_core_num = 0;

  int opt;
  while ((opt = getopt(argc, argv, "rhpdw:c:o:b:f:s:t:")) != -1) {
    switch (opt) {
      case 'w': {
        workload_config_filename = optarg;
        break;
      }
      case 'd': {
        dram_only = true;
        break;
      }
      case 'c': {
        param.logical_core_num = stoi(optarg);
        break;
      }
      case 'o': {
        param.status_tty_name = optarg;
        break;
      }
      case 'b': {
        baseline_config_filename = optarg;
        break;
      }
      case 't': {
        setting_config_filename = optarg;
      }
      case 'p': {
        print_timing_model = true;
        break;
      }
      case 'f': {
        bench = optarg;
        break;
      }
      case 'r': {
        dram_baseline = true;
        break;
      }
      case 's' :{
        int type = stoi(optarg);
        if (type==1)
        {
          n_type = nand_type::SLC_NAND;
        }
        else if (type==2)
        {
          n_type = nand_type::MLC_NAND;
        }
        else if (type==3)
        {
          n_type = nand_type::TLC_NAND;
        }
        break;
      }

      case '?':
      case 'h':
      default: {
        printf("Help\n");
        printf("  -h  print this, the help message\n");
        printf("  -w  workload config file\n");
        printf("  -b  baseline config file\n");
        printf("  -c  number of cores\n");
        printf("  -o  output status redirect tty\n");
        printf("  -f  output file name\n");
        printf("  -p  print timing model\n");
        return -1;
      }
    }
  }

  std::string t_policy_name;
  t_policy = Thread_Policy_enum::RR; //Default

  // exit if config file does not exist
    std::ifstream bconfig_file(baseline_config_filename);
    if (!bconfig_file.good()) {
        printf("Config file <%s> does not exist\n", baseline_config_filename.c_str());
        assert(false);
    }
    // parse config file
    std::string line; 
    std::string command;
    std::string value;
    printf("\nConfigs:\n");
    while (std::getline(bconfig_file, line)) {
        std::stringstream ss(line);
        command.clear();
        value.clear();
        ss >> command >> value;
        if (command != "#" && command != "")
            printf("  %25s: <%s>\n", command.c_str(), value.c_str());

        // baseline settings
        if (command == "promotion_enable")              { promotion_enable = std::stoi(value) != 0; }
        else if (command == "write_log_enable")         { write_log_enable = std::stoi(value) != 0; }
        else if (command == "tpp_enable")               { tpp_enable = std::stoi(value) != 0; }
        else if (command == "astriflash_enable")        { astriflash_enable = std::stoi(value) != 0; }
        else if (command == "t_policy")                 { t_policy_name = value; }
        else if (command == "device_triggered_ctx_swt") { device_triggered_ctx_swt = std::stoi(value) != 0; }
        else if (command == "cs_threshold")             { cs_threshold = std::stoul(value); }
        else if (command == "dram_only")                { dram_baseline = std::stoi(value) != 0; }
        // size settings
        else if (command == "ssd_cache_size_byte")      { ssd_cache_size_byte = std::stoul(value); }
        else if (command == "ssd_cache_way")            { ssd_cache_way = std::stoi(value); }
        else if (command == "host_dram_size_byte")      { host_dram_size_byte = std::stoul(value); }
        // comments or empty line
        else if (command == "#" || command == "")       {}
        else {
          printf("Error: Invalid config entry <%s>, aborting...\n", command.c_str());
          assert(false);
        }
    }

  if (t_policy_name == "RR") {
    t_policy = Thread_Policy_enum::RR;
  } else if (t_policy_name == "RANDOM") {
    t_policy = Thread_Policy_enum::RANDOM;
  } else if (t_policy_name == "LOCALITY"){
    t_policy = Thread_Policy_enum::LOCALITY;
  } else if (t_policy_name == "FAIRNESS"){
    t_policy = Thread_Policy_enum::FAIRNESS;
  }


  context_sw_buffer = device_triggered_ctx_swt;


  // exit if config file does not exist
    std::ifstream wconfig_file(workload_config_filename);
    if (!wconfig_file.good()) {
        printf("Config file <%s> does not exist\n", workload_config_filename.c_str());
        assert(false);
    }
    // parse config file
    printf("\nConfigs:\n");
    while (std::getline(wconfig_file, line)) {
        std::stringstream ss(line);
        command.clear();
        value.clear();
        ss >> command >> value;
        if (command != "#" && command != "")
            printf("  %25s: <%s>\n", command.c_str(), value.c_str());

        // workload settings
        if (command == "trace_location")              { param.filename_prefix = value; }
        else if (command == "num_files")         { param.ntraces = std::stoi(value); }
        else if (command == "num_initial_threads") { param.spawn_first = std::stoi(value); }
        else if (command == "num_mark")             { mark_inst_num = std::stoul(value); }
        else if (command == "num_sim_threads")       { param.sim_thread_num = std::stoi(value); }
        else if (command == "scale_factor")             { param.time_scale = std::stod(value); }
        // comments or empty line
        else if (command == "#" || command == "")       {}
        else {
          printf("Error: Invalid config entry <%s>, aborting...\n", command.c_str());
          assert(false);
        }
    }

    // assert(mark_inst_num > 0);


  // exit if config file does not exist
    std::ifstream tconfig_file(setting_config_filename);
    if (!tconfig_file.good()) {
        printf("No setting config files, using default values\n");
    }
    else
    {
      // parse config file
      std::string line; 
      std::string command;
      std::string value;
      printf("\nConfigs:\n");
      while (std::getline(tconfig_file, line)) {
          std::stringstream ss(line);
          command.clear();
          value.clear();
          ss >> command >> value;
          if (command != "#" && command != "")
              printf("  %25s: <%s>\n", command.c_str(), value.c_str());

          // baseline settings
          if (command == "host_dram_size_byte")      { host_dram_size_byte = std::stoul(value); }
          else if (command == "ssd_host_rate")            { ssd_host_rate = std::stof(value); ssd_cache_size_byte = (long) (host_dram_size_byte * ssd_host_rate);}
          else if (command == "cs_threshold")             { cs_threshold = std::stoul(value); }
          else if (command == "write_log_ratio")          { write_log_ratio = std::stof(value); }
          // comments or empty line
          else if (command == "#" || command == "")       {}
          else {
            printf("Error: Invalid config entry <%s>, aborting...\n", command.c_str());
            assert(false);
          }
      }
    }
    

  // if (!use_macsim)
  // {
    parameter_validation(&param);

  // }
  // else
  // {
  //   for (int file_idx = 0; file_idx < param.ntraces; file_idx++) {
  //     string cur_filename = param.filename_prefix + to_string(file_idx);
  //     param.trace_filenames.push_back(cur_filename);
  //     printf("Filename: %s\n", cur_filename.c_str());
  //   }
  // }
  ordered_memory_space.clear();
  NUMA_scan_set.clear();
  LRU_active_list.clear();
  LRU_inactive_list.clear();
  
  
  std::string bench_wmp = warmed_up_mode ? ("../output/warmup_traces/" + bench) : ("../output/" + bench);
  bench = "../output/" + bench;
  main_filename = bench;

  if (dram_only || dram_baseline)
  {
    main_filename = main_filename + "-DRAM";
  }

  output_file = fopen(main_filename.c_str(), "w");
  

  FILE* prefill_and_warmup_data_file;

  FILE* prefill_data_file_read;

  // auto it = std::next(baseline_config_filename.begin(), baseline_config_filename.find('.'));
  // baseline_config_filename.erase(it, std::next(it, 7));
  // auto it2 = std::next(workload_config_filename.begin(), workload_config_filename.find('.'));
  // workload_config_filename.erase(it2, std::next(it2, 7));
  // bench = baseline_config_filename + workload_config_filename;
  // std::cout<<bench;

  if (!write_log_enable)
  {
    write_log_ratio = 0;
  }
  
  sim_clock the_clock(0, 1);
  the_clock_pt = &the_clock;

  need_mark = false;


  bytefs_log("Data fill BEGIN");
  uint64_t start_timestamp = (uint64_t) -1;

  workload_name = extractFilename(workload_config_filename);
  size_t last_dot = workload_name.find_last_of('.');
    
  // Extract the substring starting from the position after the last slash
  if (last_dot != std::string::npos && last_dot < workload_name.length() - 1) {
      workload_name = workload_name.substr(0, last_dot);
  }

  std::string warmup_trace_workload_name = "../output/warmup_traces/" + workload_name;
  workload_name = "../output/" + workload_name;

  string workload_dram_prefill = warmup_trace_workload_name + "_prefill_data.txt";


  if (!dram_baseline) 
  {
    prefill_data_file_read = fopen((workload_dram_prefill).c_str(), "r");
    if (prefill_data_file_read != NULL)
    {
      if (dram_only)
      {
        return 0;
      }
      
      uint64_t page;
      uint64_t prefill_size = 0;
      assert(fscanf(prefill_data_file_read, "%ld", &prefill_size));
      for (size_t i = 0; i < prefill_size; i++)
      {
        assert(fscanf(prefill_data_file_read, "%ld", &page));
        prefill_pages.insert(page);
      }
      fclose(prefill_data_file_read);
    }
    else
    {
      goto DRAM_profiling;
    }
    
  }
  else
  {

DRAM_profiling:
    if (use_macsim)
    {
      //TODO: scan the working set

      // process_s* the_process = sim->m_sim_processes[0];
      // for (size_t i = 0; i < the_process->m_no_of_threads; i++)
      // {
      //   thread_s* the_thread = the_process->m_thread_trace_info[i];
      //   int trace_size = CPU_TRACE_SIZE;
      //   void* buf = new char[CPU_TRACE_SIZE];
      //   assert(the_thread->m_trace_file != nullptr);
      //   while (gzread(the_thread->m_trace_file, buf, trace_size) > 0)
      //   {
      //     trace_info_cpu_s *prev_trace_info =
      //       static_cast<trace_info_cpu_s *>(buf);
      //       inst_info_s *info = new inst_info_s();
      //     skybyte_convert_dyn_uop(info, buf,);

      //   }
      //   // Reset the gzFile to the beginning of the file
      //   if (gzrewind(the_thread->m_trace_file) != 0) {
      //       std::cerr << "Error: Failed to rewind file." << std::endl;
      //       return 1;
      //   }
        
      // }
      sim_clock the_clock_1(0, 1);

      pinatrace_drive = false;

      bytefs_log("SSD init BEGIN");
      ssd_init();
      bytefs_log("SSD init END");

      the_clock_pt = &the_clock_1;

      vector<logical_core_returns> thread_returns_list;
      ThreadScheduler scheduler_1(param.trace_filenames, param.logical_core_num, start_timestamp,
                              rr_timeslice_nano, ctx_swh_deadtime_nano,
                              1.0 / param.time_scale, param.spawn_first);
      scheduler_1.setOutputFile(param.status_tty, param.status_tty_name);
      scheduler_1.spawnServiceThread();
      skybyte_scheduler_pt = &scheduler_1;
      bytefs_log("Scheduler_1 online");
      sleep(1);

      reset_ssd_stat();
      turn_on_stat();

      scheduler_1.startExecution();

      macsim_c* sim_p;

      // Instantiate
      sim_p = new macsim_c();

      // Initialize Simulation State
      sim_p->initialize(argc, argv);

      prefill_pass = true;

      // Run simulation
      // report("run core (single threads)");
      while (sim_p->run_a_cycle_prefill())
        ;

      // Finialize Simulation State
      sim_p->stat_stalls(output_file);
      sim_p->finalize();


      scheduler_1.getThreadsProgresses(1);
      fprintf(param.status_tty, "%sDone\n", ansi_clearline);
      bytefs_log("Scheduler_1 and logical core threads all finished");

      the_clock_pt->force_finish();

      scheduler_1.joinAll(thread_returns_list);
      bytefs_log("Scheduler and logical core threads all joined");

    
      prefill_pass = false;
      the_clock_pt->force_finish();
      delete sim_p;
      
    }
    

    prefill_and_warmup_data_file = fopen((workload_dram_prefill).c_str(), "w");
  
    // Define a vector to store elements in order
      ordered_vector.clear();

      // Reserve space in the vector to avoid reallocations
      ordered_vector.reserve(prefill_pages.size());

      // Traverse the unordered set and insert elements into the ordered vector
      for (const auto& element : prefill_pages) {
          ordered_vector.push_back(element);
      }

      // Sort the vector to maintain the order
      std::sort(ordered_vector.begin(), ordered_vector.end());

      std::cout << ordered_vector.front() << std::endl;
      std::cout << ordered_vector.back() << std::endl;
    
    fprintf(prefill_and_warmup_data_file, "%ld\n", ordered_vector.size());

    for (auto page : ordered_vector) {
      fprintf(prefill_and_warmup_data_file, "%ld\n", page);
    }

    fclose(prefill_and_warmup_data_file);

    if (dram_only || dram_baseline)
    {
      print_stat();
      return 0;
    }

  }
  //Start the warmup pass!

  FILE* warmup_hint_data_file = fopen((bench_wmp + "_warmup_hint_dram_system.txt").c_str(),"r");
  if (warmup_hint_data_file == NULL) //Need to run one time warmup with pinatrace
  {
    the_clock_pt = &the_clock;

    pinatrace_drive = true;

    bytefs_log("SSD init BEGIN");
    ssd_init();
    bytefs_log("SSD init END");
    

    // Define a vector to store elements in order
      ordered_vector.clear();

      // Reserve space in the vector to avoid reallocations
      ordered_vector.reserve(prefill_pages.size());

      // Traverse the unordered set and insert elements into the ordered vector
      for (const auto& element : prefill_pages) {
          ordered_vector.push_back(element);
      }

      // Sort the vector to maintain the order
      std::sort(ordered_vector.begin(), ordered_vector.end());

    std::cout << ordered_vector.front() << std::endl;
    std::cout << ordered_vector.back() << std::endl;

    size_t total_footprint_kb = ordered_vector.size() * 4096 / 1024;
    fprintf(output_file, "Total Data Footprint: %ld kB (%.3f GB)\n",
        total_footprint_kb, total_footprint_kb / 1024.0 / 1024.0);

    for (auto page : ordered_vector) {
      bytefs_fill_data(page);
    }
    
    //First time prefill (for the incoming macsim trace) done!

    //Now starts to prefill the data for the warmup pass (pinatrace trace)
    bytefs_log("Data fill BEGIN");
    start_timestamp = (uint64_t) -1;
    unordered_set<uint64_t> pinatrace_pages;
    for (string filename : param.trace_filenames) {
      TraceReplayUnit tru;
      trace_sample *sample;
      tru.init(filename);
      printf("Loading file %s\n", filename.c_str());
      while ((sample = tru.getCurrentTraceSample()) != nullptr) {
        if (sample->r == 'R' || sample->r == 'W') {
          start_timestamp = min(start_timestamp, sample->ns);
          pinatrace_pages.insert(sample->addr / PG_SIZE * PG_SIZE);
        }
        tru.getNextTraceSample();
      }
      tru.deinit();
    }

    for (auto page : pinatrace_pages) {
      bytefs_fill_data(page);
    }

    if (tpp_enable)
    {
      std::sort(ordered_memory_space.begin(), ordered_memory_space.end());
    }

    ssd_backend_reset_timestamp();

    pinatrace_pages.clear();


      //First time wram up run begins
      device_triggered_ctx_swt = false; //don't need to do context switch when running the warmup pass

      vector<logical_core_returns> thread_returns_list_1;
      // need_mark =true;
      ThreadScheduler scheduler_2(param.trace_filenames, param.logical_core_num, start_timestamp,
                              rr_timeslice_nano, ctx_swh_deadtime_nano,
                              1.0 / param.time_scale, param.spawn_first);
      scheduler_2.setOutputFile(param.status_tty, param.status_tty_name);
      scheduler_2.spawnServiceThread();
      skybyte_scheduler_pt = &scheduler_2;
      bytefs_log("Scheduler_2 online");
      sleep(1);

      reset_ssd_stat();
      turn_on_stat();
      bytefs_log("Threads start signaling...");

      scheduler_2.startExecution();

      ssd *ssd = &gdev;
      ssd->run_flag = 1;

      // macsim_c* sim_p2;

      // // Instantiate
      // sim_p2 = new macsim_c();

      // // Initialize Simulation State
      // sim_p2->initialize(argc, argv);

      // prefill_pass = false;

      // // Run simulation
      // // report("run core (single threads)");
      // while (sim_p2->run_a_cycle())
      //   ;

      // // Finialize Simulation State
      // // sim_p2->stat_stalls(output_file);
      // sim_p2->finalize();

      // wait for thread execution
      const double print_interval_hi = 0.5e9;
      const double print_interval_lo = 0.5e9;
      double progress;
      while (1 - (progress = scheduler_2.getThreadsProgresses(0)) > 1e-12) {
        uint64_t elapsed_time = scheduler_2.getTimeSinceStart();
        uint64_t estimated_completion_time = elapsed_time / progress;
        uint64_t sleep_ns = max(min(0.1 * estimated_completion_time, print_interval_hi), print_interval_lo);
        sleepns(sleep_ns);
      }


      scheduler_2.getThreadsProgresses(1);
      fprintf(param.status_tty, "%sDone\n", ansi_clearline);
      bytefs_log("Scheduler_2 and logical core threads all finished");

      // the_clock_pt->force_finish();

      scheduler_2.joinAll(thread_returns_list_1);
      bytefs_log("Scheduler_2 and logical core threads all joined");


      prefill_pass = false;
      // the_clock_pt->force_finish();
      bytefs_stop_threads_gracefully();
      bytefs_log("Service threads stopped");

      //copy the caches in the DRAM subsystem
      warmup_hint_data_file = fopen((bench_wmp + "_warmup_hint_dram_system.txt").c_str(),"w");
      copy_dram_system(warmup_hint_data_file);

      //TPP only: copy states in TPP system
      if (tpp_enable)
      {
        FILE* warmup_hint_data_file_tpp = fopen((bench_wmp + "_warmup_hint_tpp_system.txt").c_str(),"w");
        copy_tpp_system(warmup_hint_data_file_tpp);
        fclose(warmup_hint_data_file_tpp);
      }

      fclose(warmup_hint_data_file);

      return 0;
  }
  else
  {
    the_clock_pt = &the_clock;

      pinatrace_drive = false;

      bytefs_log("SSD init BEGIN");
      ssd_init();
      bytefs_log("SSD init END");
      

      // Define a vector to store elements in order
        ordered_vector.clear();

        // Reserve space in the vector to avoid reallocations
        ordered_vector.reserve(prefill_pages.size());

        // Traverse the unordered set and insert elements into the ordered vector
        for (const auto& element : prefill_pages) {
            ordered_vector.push_back(element);
        }

        // Sort the vector to maintain the order
        std::sort(ordered_vector.begin(), ordered_vector.end());

      std::cout << ordered_vector.front() << std::endl;
      std::cout << ordered_vector.back() << std::endl;

      size_t total_footprint_kb = ordered_vector.size() * 4096 / 1024;
      fprintf(output_file, "Total Data Footprint: %ld kB (%.3f GB)\n",
          total_footprint_kb, total_footprint_kb / 1024.0 / 1024.0);

      for (auto page : ordered_vector) {
        bytefs_fill_data(page);
      }

      //First time prefill (for the incoming macsim trace) done!

      replay_dram_system(warmup_hint_data_file);
      if (tpp_enable)
      {
        FILE* warmup_hint_data_file_tpp = fopen((bench_wmp + "_warmup_hint_tpp_system.txt").c_str(),"r");
        replay_tpp_system(warmup_hint_data_file_tpp);
        fclose(warmup_hint_data_file_tpp);
      }

      std::cout<<"DRAM Subsystem warmup Replay done!"<<std::endl;

      if (tpp_enable)
      {
        std::sort(ordered_memory_space.begin(), ordered_memory_space.end());
      }

      ssd_backend_reset_timestamp();


    FILE* warmup_hint_data_file_2 = fopen((bench_wmp + "_warmup_hint_data_wlog.txt").c_str(),"r");
    if (warmup_hint_data_file_2 == NULL && write_log_enable)
    {
      //Second time wram up run begins

      vector<logical_core_returns> thread_returns_list_2;
      // need_mark =true;
      ThreadScheduler scheduler_3(param.trace_filenames, param.logical_core_num, start_timestamp,
                              rr_timeslice_nano, ctx_swh_deadtime_nano,
                              1.0 / param.time_scale, param.spawn_first);
      scheduler_3.setOutputFile(param.status_tty, param.status_tty_name);
      scheduler_3.spawnServiceThread();
      skybyte_scheduler_pt = &scheduler_3;
      bytefs_log("Scheduler_3 online");
      sleep(1);

      reset_ssd_stat();
      turn_on_stat();
      bytefs_log("Threads start signaling...");

      scheduler_3.startExecution();

      ssd *ssd = &gdev;
      ssd->run_flag = 1;

      macsim_c* sim_p2;

      // Instantiate
      sim_p2 = new macsim_c();

      // Initialize Simulation State
      sim_p2->initialize(argc, argv);

      prefill_pass = false;

      // Run simulation
      // report("run core (single threads)");
      while (sim_p2->run_a_cycle())
        ;

      // Finialize Simulation State
      // sim_p2->stat_stalls(output_file);
      sim_p2->finalize();

      // scheduler_3.getThreadsProgresses(1);
      fprintf(param.status_tty, "%sDone\n", ansi_clearline);
      bytefs_log("Scheduler_3 and logical core threads all finished");

      the_clock_pt->force_finish();

      scheduler_3.joinAll(thread_returns_list_2);
      bytefs_log("Scheduler_3 and logical core threads all joined");


      prefill_pass = false;
      the_clock_pt->force_finish();
      bytefs_stop_threads_gracefully();
      bytefs_log("Service threads stopped");
      delete sim_p2;

      uint64_t read_pgnum = 0;
      uint64_t write_pgnum = 0;
      uint64_t tmp_array[64];
      for (size_t i = 0; i < 64; i++)
      {
        tmp_array[i] = 0;
      }
      
      if (write_log_enable)
      {
        std::pair<uint64_t, uint64_t> read_write_num = flush_log_region_warmup(&gdev, tmp_array);
        read_pgnum = read_write_num.first;
        write_pgnum = read_write_num.second;
      }
      warmup_hint_data_file_2 = fopen((bench_wmp + "_warmup_hint_data_wlog.txt").c_str(),"w");

      fprintf(warmup_hint_data_file_2, "%ld\n", read_pgnum);
      fprintf(warmup_hint_data_file_2, "%ld\n", write_pgnum);
      for (size_t i = 0; i < 64; i++)
      {
        fprintf(warmup_hint_data_file_2, "%ld\n", tmp_array[i]);
      }

      fclose(warmup_hint_data_file_2);

      print_stat();
      return 0;

    }
    else
    {  
        need_mark = false;

        reset_ssd_stat();
        turn_on_stat();

        if (write_log_enable)
        {
          uint64_t read_pgnum = 0;
          uint64_t write_pgnum = 0;
          uint64_t tmp_array[64];


          assert(fscanf(warmup_hint_data_file_2, "%ld", &read_pgnum));
          assert(fscanf(warmup_hint_data_file_2, "%ld", &write_pgnum));
          for (size_t i = 0; i < 64; i++)
          {
            assert(fscanf(warmup_hint_data_file_2, "%ld", &tmp_array[i]));
          } 
          //Warmup for write log
          warmup_write_log(read_pgnum, write_pgnum);

          for (size_t i = 0; i < 64; i++)
          {
            SSD_STAT_ADD(byte_issue_nand_wr_modified_distribution[i], tmp_array[i]);
          }

        }
        
        // warmup_ssd_dram(warmup_dirty_ratio_cache, warmup_dirty_ratio_dram, read_pgnum, write_pgnum, cache_overall_cover_rate, 
        // host_overall_cover_rate, cache_uncovered_dirty_rate, host_uncovered_dirty_rate);
        

            // while (!prefill_terminate) {
                      // prefill_terminate = backend_prefill_data(page); //TODO: addback prefill for GC ? put in the function
              // if (prefill_terminate)
              //   break;

      // }
      
      // sim_clock the_clock_2(0, 1);
      // the_clock_pt = &the_clock_2;
      // ssd_reset_skybyte();

      if (tpp_enable)
      {
        std::sort(ordered_memory_space.begin(), ordered_memory_space.end());
      }

      prefill_pages.clear();
      ordered_vector.clear();

      ssd *ssd = &gdev;

      vector<logical_core_returns> thread_returns_list;
      ThreadScheduler scheduler(param.trace_filenames, param.logical_core_num, start_timestamp,
                                rr_timeslice_nano, ctx_swh_deadtime_nano,
                                1.0 / param.time_scale, param.spawn_first);
      scheduler.setOutputFile(param.status_tty, param.status_tty_name);
      scheduler.spawnServiceThread();
      skybyte_scheduler_pt = &scheduler;
      bytefs_log("Scheduler online");
      sleep(1);

      

      scheduler.startExecution();

      
      ssd->run_flag = 1;

    
      macsim_c* sim;

      // Instantiate
      sim = new macsim_c();

      // Initialize Simulation State
      sim->initialize(argc, argv);

      // Run simulation
      // report("run core (single threads)");
      while (sim->run_a_cycle())
        ;
      
      sim->stat_stalls(output_file);

      // Finialize Simulation State
      sim->finalize();


      scheduler.getThreadsProgresses(1);
      fprintf(param.status_tty, "%sDone\n", ansi_clearline);
      bytefs_log("Scheduler and logical core threads all finished");

      scheduler.joinAll(thread_returns_list);
      bytefs_log("Scheduler and logical core threads all joined");

      the_clock_pt->force_finish();
      bytefs_stop_threads_gracefully();
      bytefs_log("Service threads stopped");

      print_stat();

      return 0;
    }
    
  }

  return 0;   
}
