import os, sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


output_dir = "../../output/"

workload_baseType3s = ["bc-8-baseType3", "bfs-dense-8-baseType3", "dlrm-8-baseType3", "radix-8-baseType3", "srad-8-baseType3", "tpcc-8-baseType3", "ycsb-8-baseType3"]
workload_dram_onlys = ["bc-8-DRAM-only-DRAM", "bfs-dense-8-DRAM-only-DRAM", "dlrm-8-DRAM-only-DRAM", "radix-8-DRAM-only-DRAM", "srad-8-DRAM-only-DRAM", "tpcc-8-DRAM-only-DRAM", "ycsb-8-DRAM-only-DRAM"]

workload_names = ["bc", "bfs-dense", "dlrm", "radix", "srad", "tpcc", "ycsb"]

# workloads = ["bfs-4-parsed", "bc-parsed", "dlrm-large", "radix-parsed", "ycsb-large", "tpcc-large"]

# workload_names = ["bfs-twitter", "bc-twitter", "dlrm-training", "radix", "ycsb-nstore", "tpcc-nstore"]

cores = ["Core 0", "Core 1", "Core 2", "Core 3", "Core 4", "Core 5", "Core 6", "Core 7"]



# designtypes = ["baseType3", "flatflash", "assd-W", "assd-WP", "assd-Full"]
# designtypes = ["baseType3", "flatflash", "assd-W", "assd-WP"]

output_file_compute = f"breakdown/compute.txt"
output_file_mem = f"breakdown/mem.txt"
# output_file_others = f"breakdown/others.txt"
fout_compute = open(output_file_compute, 'w')
fout_mem = open(output_file_mem, 'w')
# fout_others = open(output_file_others, 'w')

# fout.write("BaseType3 | FlatFlash-CXL | SkyByte-W | SkyByte-WP | SkyByte-Full\n\n")
fout_compute.write("DRAM | CXL-SSD\n\n")
fout_mem.write("DRAM | CXL-SSD\n\n")
# fout_others.write("DRAM | CXL-SSD\n\n")


for i in range(len(workload_baseType3s)):
    fout_compute.write(workload_names[i]+"\n")
    fout_mem.write(workload_names[i]+"\n")
    # fout_others.write(workload_names[i]+"\n")
    # for j in range(len(designtypes)):
    data_file = output_dir + workload_baseType3s[i]
    dram_only_file = output_dir + workload_dram_onlys[i]

    find1 = False
    find2 = False
    # print(data_file)
    
    if os.path.exists(dram_only_file):
        f = open(dram_only_file, "r")
        lines = f.read()
        lines = lines.strip().split("\n")
        line_i = -1
        for line in lines:
            line_i += 1
            terms = line.split(":")
            if terms[0] == "Core 0 stalls(mem)":
                find2 = True
                break
        print(lines[line_i])
        
        if find2:
            mem = 0
            compute = 0
            others = 0
            for j in range(8):
                for k in range(3):
                    terms = lines[line_i+j*3+k].split(":")
                    if k==0:
                        mem += int(terms[1].strip())
                    elif k==1:
                        compute += int(terms[1].strip())
                    else:
                        compute += int(terms[1].strip())
            fout_compute.write(str(compute)+" ")
            fout_mem.write(str(mem)+" ")
            # fout_others.write(str(others)+" ")
            
            line_i += 24
    
    
    
    if os.path.exists(data_file):
        f = open(data_file, "r")
        lines = f.read()
        lines = lines.strip().split("\n")
        line_i = -1
        for line in lines:
            line_i += 1
            terms = line.split(":")
            if terms[0] == "Core 0 stalls(mem)":
                find1 = True
                break
        print(lines[line_i])
        
        if find1:
            mem = 0
            compute = 0
            others = 0
            for j in range(8):
                for k in range(3):
                    terms = lines[line_i+j*3+k].split(":")
                    if k==0:
                        mem += int(terms[1].strip())
                    elif k==1:
                        compute += int(terms[1].strip())
                    else:
                        compute += int(terms[1].strip())
            fout_compute.write(str(compute)+" ")
            fout_mem.write(str(mem)+" ")
            # fout_others.write(str(others)+" ")
            
            line_i += 24
    
    
    fout_compute.write("\n\n")
    # fout_others.write("\n\n")
    fout_mem.write("\n\n")
        
        # for line in lines[line_i+1:]:
        #     line_i += 1
        #     terms = line.split(":")
        #     if terms[0] == "Core 0 stalls(mem)":
        #         find2 = True
        #         break
        # print(lines[line_i])
        
        
        # if find2:
        
        #     mem = 0
        #     compute = 0
        #     others = 0
        #     for j in range(8):
        #         for k in range(3):
        #             terms = lines[line_i+j*3+k].split(":")
        #             if k==0:
        #                 mem += int(terms[1].strip())
        #             elif k==1:
        #                 compute += int(terms[1].strip())
        #             else:
        #                 compute += int(terms[1].strip())
        #     fout_compute.write(str(compute)+" ")
        #     fout_mem.write(str(mem)+" ")
        #     # fout_others.write(str(others)+" ")
            
        #     line_i += 12
        
            
        
        
        # # print(lines)
        # lines = lines.strip().split("\n")
        # find1 = False
        # find2 = False
        # count = 0
        # meet = False
        # for line in lines:
        #     terms = line.split("Core")
        #     if terms[0]=="**":
        #         # avg_ltc = terms[1].strip()
        #         print(terms[2].strip())
        #         s_terms = terms[2].strip().split()
        #         for s_term in s_terms:
        #             if s_term.startswith("cycles:"):
        #                 print(s_term.split(":")[1].strip())
        #                 longest_exe_time = max(int(s_term.split(":")[1].strip()),longest_exe_time)
        #         meet = True
        #         if count == 0:
        #             find1 = True
        #         if count == 1:
        #             find2 = True
        #     else:
        #         if meet:
        #             print(longest_exe_time)
        #             count += 1
        #             if count == 1:
        #                 time1 = longest_exe_time
        #             if count == 2:
        #                 time2 = longest_exe_time
        #             longest_exe_time = 0
        #             meet = False
        # if find1:
        #     fout.write(str(time1)+" ")
        # else:
        #     fout.write("inf ")
        # if find2:
        #     fout.write(str(time2)+" ")
        # else:
        #     fout.write("inf ")
    # else:
    #     fout.write("inf ")
    # fout.write("\n\n")