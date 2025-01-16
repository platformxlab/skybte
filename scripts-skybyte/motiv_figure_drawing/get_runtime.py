import os, sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


output_dir = "../../output/"

workloads = ["bfs-4-parsed", "bc-parsed", "dlrm-large", "barnes-large", "radix-parsed", "srad-4-large", "ycsb-large", "tpcc-large"]

workload_names = ["bfs-twitter", "bc-twitter", "dlrm-training", "barnes", "radix", "srad", "ycsb-nstore", "tpcc-nstore"]

# designtypes = ["baseType3", "flatflash", "assd-W", "assd-WP", "assd-Full"]
designtypes = ["baseType3", "flatflash", "assd-W", "assd-WP"]

output_file = f"exetime.txt"
fout = open(output_file, 'w')

# fout.write("BaseType3 | FlatFlash-CXL | SkyByte-W | SkyByte-WP | SkyByte-Full\n\n")
fout.write("BaseType3 | FlatFlash-CXL | SkyByte-W | SkyByte-WP\n\n")

for i in range(len(workloads)):
    fout.write(workload_names[i]+"\n")
    for j in range(len(designtypes)):
        data_file = output_dir + workloads[i] + "-" + designtypes[j]
        # print(data_file)
        if os.path.exists(data_file):
            f = open(data_file, "r")
            lines = f.read()
            # print(lines)
            lines = lines.strip().split("\n")
            find = False
            for line in lines:
                terms = line.split(":")
                if terms[0]=="Program_Finish_Time(Real)":
                    avg_ltc = terms[1].strip()
                    find = True
            if find:
                fout.write(avg_ltc+" ")
            else:
                fout.write("inf ")
        else:
            fout.write("inf ")
    fout.write("\n\n")