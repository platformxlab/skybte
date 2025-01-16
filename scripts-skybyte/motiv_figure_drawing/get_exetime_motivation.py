import os, sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


output_dir = "../../output/"

workload_baseType3s = ["bc-8-baseType3", "bfs-dense-8-baseType3", "dlrm-8-baseType3", "radix-8-baseType3", "srad-8-baseType3", "tpcc-8-baseType3", "ycsb-8-baseType3"]
workload_dram_onlys = ["bc-8-DRAM-only-DRAM", "bfs-dense-8-DRAM-only-DRAM", "dlrm-8-DRAM-only-DRAM", "radix-8-DRAM-only-DRAM", "srad-8-DRAM-only-DRAM", "tpcc-8-DRAM-only-DRAM", "ycsb-8-DRAM-only-DRAM"]

workload_names = [ "bc", "bfs-dense", "dlrm", "radix", "srad", "ycsb", "tpcc"]

# designtypes = ["baseType3", "flatflash", "assd-W", "assd-WP", "assd-Full"]
# designtypes = ["baseType3", "flatflash", "assd-W", "assd-WP"]

output_file = f"exetime.txt"
fout = open(output_file, 'w')

# fout.write("BaseType3 | FlatFlash-CXL | SkyByte-W | SkyByte-WP | SkyByte-Full\n\n")
fout.write("DRAM Memory | Baseline CXL-SSD\n\n")

for i in range(len(workload_baseType3s)):
    fout.write(workload_names[i]+"\n")
    # for j in range(len(designtypes)):
    dram_only_file = output_dir + workload_dram_onlys[i]
    baseline_file = output_dir + workload_baseType3s[i]
    longest_exe_time = 0
    time1 = 0
    time2 = 0
    # print(data_file)
    if os.path.exists(dram_only_file):
        f = open(dram_only_file, "r")
        lines = f.read()
        # print(lines)
        lines = lines.strip().split("\n")
        find1 = False
        find2 = False
        count = 0
        meet = False
        for line in lines:
            terms = line.split("Core")
            if terms[0]=="**":
                # avg_ltc = terms[1].strip()
                print(terms[2].strip())
                s_terms = terms[2].strip().split()
                for s_term in s_terms:
                    if s_term.startswith("cycles:"):
                        print(s_term.split(":")[1].strip())
                        longest_exe_time = max(int(s_term.split(":")[1].strip()),longest_exe_time)
                meet = True
                if count == 0:
                    find1 = True

            else:
                if meet:
                    print(longest_exe_time)
                    count += 1
                    if count == 1:
                        time1 = longest_exe_time

                    longest_exe_time = 0
                    meet = False
        if find1:
            fout.write(str(time1)+" ")
        else:
            fout.write("inf ")
    
    
    if os.path.exists(baseline_file):
        f = open(baseline_file, "r")
        lines = f.read()
        # print(lines)
        lines = lines.strip().split("\n")
        find1 = False
        find2 = False
        count = 0
        meet = False
        for line in lines:
            terms = line.split("Core")
            if terms[0]=="**":
                # avg_ltc = terms[1].strip()
                print(terms[2].strip())
                s_terms = terms[2].strip().split()
                for s_term in s_terms:
                    if s_term.startswith("cycles:"):
                        print(s_term.split(":")[1].strip())
                        longest_exe_time = max(int(s_term.split(":")[1].strip()),longest_exe_time)
                meet = True
                if count == 0:
                    find1 = True

            else:
                if meet:
                    print(longest_exe_time)
                    count += 1
                    if count == 1:
                        time1 = longest_exe_time

                    longest_exe_time = 0
                    meet = False
        if find1:
            fout.write(str(time1)+" ")
        else:
            fout.write("inf ")
                    
    else:
        fout.write("inf ")
    fout.write("\n\n")