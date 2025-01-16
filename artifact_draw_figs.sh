git submodule update --init

cd scripts-skybyte/motiv_figure_drawing

# Draw figure 2
python3 get_exetime_motivation.py
python3 overallExetime.py   # figure 2 is output/Exetime.pdf

# Draw figure 3
python3 LatencyCDF_motivation.py   # figure 3 is output/LatencyCDF.pdf

# Draw figure 4
python3 get_cpu_breakdown_motivation.py   # figure 4 is output/Breakdown.pdf
python3 CPUBreakdown.py


cd ../..
cd figure_drawing

# Draw figure 14
python3 run.py -d e2e_perf
cd e2e_perf

python3 run.py  # figure 14 is e2e_perf/OverallPerf.pdf
cd ..

# Draw figure 15
python3 run.py -d nthreads
cd nthreads

python3 run.py  # figure 15 is nthreads/throughput_threads.pdf
cd ..

# Draw figure 16
python3 run.py -d pancake
cd pancake

python3 run.py  # figure 16 is output.pdf
cd ..

# Draw figure 17
python3 run.py -d mem_latency
cd mem_latency

python3 run_a.py  # figure 17.(a) is mem_latency_breakdown_Base-CSSD.pdf
python3 run_b.py  # figure 17.(b) is mem_latency_breakdown_respective.pdf
cd ..

# Draw figure 18
python3 run.py -d nwrites
cd nwrites

python3 run.py  # figure 18 is nwrites.pdf
cd ..


# For evaluation, you can check the figures in the corresponding directories. Besides, you can also check the "std" data files in each output directory (if there is one).
# Note that our simulator is non-deterministic, so the figures may have slight differences from the ones in the paper.


