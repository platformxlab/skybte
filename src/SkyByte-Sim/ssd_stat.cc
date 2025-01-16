#include "ssd_stat.h"
#include "cache_controller.h"

extern FILE *output_file;
extern std::string main_filename;

int64_t mark_count = 0;
int64_t evict_mark_count = 0;


// internal stat counters:
ssd_stat stat;
int stat_flag = 0;

extern cache_controller* dram_subsystem;

int turn_on_stat(void) {
    memset(&stat, 0, sizeof(ssd_stat));
    stat_flag = 1;
    return 0;
}

int reset_ssd_stat(void) {
    memset(&stat, 0, sizeof(ssd_stat));
    stat_flag = 0;
    return 0;
}

int print_stat(void) {
    printf("============= ByteFS report =============\n");
    // log based stats
    int short_field_len = 10;
    int long_field_len = 20;
    printf("  Issue count\n");
    printf("    Block issue count:      %-*lu = R: %-*lu + W: %-*lu \n", 
            short_field_len, stat.block_rissue_count.load() + stat.block_wissue_count.load(),
            short_field_len, stat.block_rissue_count.load(),
            short_field_len, stat.block_wissue_count.load());
    printf("    Byte issue count:       %-*lu = R: %-*lu + W: %-*lu \n", 
            short_field_len, stat.byte_rissue_count.load() + stat.byte_wissue_count.load(),
            short_field_len, stat.byte_rissue_count.load(),
            short_field_len, stat.byte_wissue_count.load());

    printf("  Traffic in bytes\n");
    printf("    Block issue traffic:    %-*lu = R: %-*lu + W: %-*lu \n", 
            long_field_len, stat.block_rissue_traffic.load() + stat.block_wissue_traffic.load(),
            long_field_len, stat.block_rissue_traffic.load(),
            long_field_len, stat.block_wissue_traffic.load());
    printf("    Byte issue traffic:     %-*lu = R: %-*lu + W: %-*lu \n", 
            long_field_len, stat.byte_rissue_traffic.load() + stat.byte_wissue_traffic.load(),
            long_field_len, stat.byte_rissue_traffic.load(),
            long_field_len, stat.byte_wissue_traffic.load());
    
    printf("  Traffic breakdown \n");
    printf("    Block data traffic:     %-*lu = R: %-*lu + W: %-*lu \n",
            long_field_len, stat.block_data_traffic_r.load()+stat.block_data_traffic_w.load(), 
            long_field_len, stat.block_data_traffic_r.load(),
            long_field_len, stat.block_data_traffic_w.load());
    printf("    Block meta traffic:     %-*lu = R: %-*lu + W: %-*lu \n",
            long_field_len, stat.block_metadata_issue_traffic_r.load()+stat.block_metadata_issue_traffic_w.load(),
            long_field_len, stat.block_metadata_issue_traffic_r.load(),
            long_field_len, stat.block_metadata_issue_traffic_w.load());
    printf("    Byte data traffic:      %-*lu = R: %-*lu + W: %-*lu \n",
            long_field_len, stat.byte_data_traffic_r.load()+stat.byte_data_traffic_w.load(),
            long_field_len, stat.byte_data_traffic_r.load(),
            long_field_len, stat.byte_data_traffic_w.load());
    printf("    Byte meta traffic:      %-*lu = R: %-*lu + W: %-*lu \n",
            long_field_len, stat.byte_metadata_issue_traffic_r.load()+stat.byte_metadata_issue_traffic_w.load(),
            long_field_len, stat.byte_metadata_issue_traffic_r.load(),
            long_field_len, stat.byte_metadata_issue_traffic_w.load());

    printf("  Metadata\n");
    printf("    Inode traffic:          %-*lu\n", long_field_len, stat.inode_traffic.load());
    printf("    Superblock traffic:     %-*lu\n", long_field_len, stat.superblock_traffic.load());
    printf("    Bitmap traffic:         %-*lu\n", long_field_len, stat.bitmap_traffic.load());
    printf("    Journal traffic:        %-*lu\n", long_field_len, stat.journal_traffic.load());
    printf("    Journal traffic:        %-*lu\n", long_field_len, stat.dp_traffic.load());

    printf("  Log\n");
    printf("    Write operation:        %-*lu\n", long_field_len, stat.log_wr_op.load());
    printf("    Read operation:         %-*lu\n", long_field_len, stat.log_rd_op.load());
    printf("    Read partial hit:       %-*lu\n", long_field_len, stat.log_rd_log_page_partial_hit.load());
    printf("    Read log hit:           %-*lu\n", long_field_len, stat.log_rd_log_page_hit.load());
    printf("    Direct NAND read:       %-*lu\n", long_field_len, stat.log_direct_rd_page.load());
    printf("    Coalescing NAND read:   %-*lu\n", long_field_len, stat.log_coalescing_rd_page.load());
    printf("    NAND write:             %-*lu\n", long_field_len, stat.log_wr_page.load());
    printf("    Log append:             %-*lu\n", long_field_len, stat.log_append.load());
    printf("    Log flush:              %-*lu\n", long_field_len, stat.log_flushes.load());

    fprintf(output_file, "Log\n");
    fprintf(output_file, "    Write operation:        %-*lu\n", long_field_len, stat.log_wr_op.load());
    fprintf(output_file, "    Read operation:         %-*lu\n", long_field_len, stat.log_rd_op.load());
    fprintf(output_file, "    Read partial hit:       %-*lu\n", long_field_len, stat.log_rd_log_page_partial_hit.load());
    fprintf(output_file, "    Read log hit:           %-*lu\n", long_field_len, stat.log_rd_log_page_hit.load());
    fprintf(output_file, "    Direct NAND read:       %-*lu\n", long_field_len, stat.log_direct_rd_page.load());
    fprintf(output_file, "    Coalescing NAND read:   %-*lu\n", long_field_len, stat.log_coalescing_rd_page.load());
    fprintf(output_file, "    NAND write:             %-*lu\n", long_field_len, stat.log_wr_page.load());
    fprintf(output_file, "    Log append:             %-*lu\n", long_field_len, stat.log_append.load());
    fprintf(output_file, "    Log flush:              %-*lu\n", long_field_len, stat.log_flushes.load());

    printf("  Page Cache\n");
    printf("    Page cache read:        %-*lu = Hit: %-*lu + Miss: %-*lu \n", 
            long_field_len, (stat.page_cache_rd_hit.load() + stat.page_cache_rd_miss.load()),
            long_field_len, stat.page_cache_rd_hit.load(),
            long_field_len, stat.page_cache_rd_miss.load());
    printf("    Page cache write:       %-*lu = Hit: %-*lu + Miss: %-*lu \n", 
            long_field_len, (stat.page_cache_wr_hit.load() + stat.page_cache_wr_miss.load()),
            long_field_len, stat.page_cache_wr_hit.load(),
            long_field_len, stat.page_cache_wr_miss.load());
    printf("    Page cache flush traffic:            %-*lu\n", long_field_len, stat.page_cache_flush_traffic.load());
    printf("    Page cache actual write traffic:     %-*lu\n", long_field_len, stat.page_cache_actuall_w_traffic.load());

    printf("  Latency\n");
    printf("    Total read latency:     %-*lu\n", long_field_len, stat.total_r_lat.load());
    printf("    Total write latency:    %-*lu\n", long_field_len, stat.total_w_lat.load());
    printf("    Total program latency:  %-*lu\n", long_field_len, stat.prog_lat.load());

    printf("  Internal Traffic\n");
    printf("    Total NAND rd user:     %-*lu\n", long_field_len, stat.nand_read_user.load());
    printf("    Total NAND rd internal: %-*lu\n", long_field_len, stat.nand_read_internal.load());
    printf("    Total NAND rd GC:       %-*lu\n", long_field_len, stat.nand_read_gc.load());
    printf("    Total NAND wr user:     %-*lu\n", long_field_len, stat.nand_write_user.load());
    printf("    Total NAND wr internal: %-*lu\n", long_field_len, stat.nand_write_internal.load());
    printf("    Total NAND wr GC:       %-*lu\n", long_field_len, stat.nand_write_gc.load());

    fprintf(output_file, "Internal Traffic\n");
    fprintf(output_file, "    Total NAND rd user:     %-*lu\n", long_field_len, stat.nand_read_user.load());
    fprintf(output_file, "    Total NAND rd internal: %-*lu\n", long_field_len, stat.nand_read_internal.load());
    fprintf(output_file, "    Total NAND rd GC:       %-*lu\n", long_field_len, stat.nand_read_gc.load());
    fprintf(output_file, "    Total NAND wr user:     %-*lu\n", long_field_len, stat.nand_write_user.load());
    fprintf(output_file, "    Total NAND wr internal: %-*lu\n", long_field_len, stat.nand_write_internal.load());
    fprintf(output_file, "    Total NAND wr GC:       %-*lu\n", long_field_len, stat.nand_write_gc.load());

    printf("========== ByteFS rw modification distribution ==========\n");
    double total_byte_issue_nand_wr_modified = 0;
    double total_byte_issue_nand_rd_modified = 0;
    for (int i = 0; i < PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY; i++) {
        total_byte_issue_nand_wr_modified += 
                stat.byte_issue_nand_wr_modified_distribution[i].load();
        total_byte_issue_nand_rd_modified += 
                stat.byte_issue_nand_rd_modified_distribution[i].load();
    }
    for (int i = 0; i < PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY; i++) {
        printf("%3.0f%%: r %7.3f%%, w %7.3f%%   ",
        // printf("%3.0f%%, %7.3f%%, %7.3f%%\n",
        100.0 * (i + 1) / (PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY),
        100.0 * stat.byte_issue_nand_wr_modified_distribution[i].load() / 
                total_byte_issue_nand_wr_modified,
        100.0 * stat.byte_issue_nand_rd_modified_distribution[i].load() / 
                total_byte_issue_nand_rd_modified);
        if (i % 4 == 3) printf("\n");
    }

    fprintf(output_file, "========== ByteFS rw modification distribution ==========\n");
    total_byte_issue_nand_wr_modified = 0;
    total_byte_issue_nand_rd_modified = 0;
    for (int i = 0; i < PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY; i++) {
        total_byte_issue_nand_wr_modified += 
                stat.byte_issue_nand_wr_modified_distribution[i].load();
        total_byte_issue_nand_rd_modified += 
                stat.byte_issue_nand_rd_modified_distribution[i].load();
    }
    for (int i = 0; i < PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY; i++) {
        fprintf(output_file, "%3.0f%%: r %7.3f%%, w %7.3f%%   ",
        // printf("%3.0f%%, %7.3f%%, %7.3f%%\n",
        100.0 * (i + 1) / (PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY),
        100.0 * stat.byte_issue_nand_wr_modified_distribution[i].load() / 
                total_byte_issue_nand_wr_modified,
        100.0 * stat.byte_issue_nand_rd_modified_distribution[i].load() / 
                total_byte_issue_nand_rd_modified);
        if (i % 4 == 3) fprintf(output_file, "\n");
    }


    printf("========== LSSD DRAM Subsystem Hit Ratio ==========\n");
    
    std::cout<<"Host_DRAM_hit rate: "<<(double)stat.host_dram_hit_num/stat.total_access_num<<std::endl;
    std::cout<<"Anyway_DRAM_hit rate: "<<(double)stat.hostandssdDram_hit_num/stat.total_access_num<<std::endl;

    fprintf(output_file, "Host_DRAM_hit rate: %f\n",(double)stat.host_dram_hit_num/stat.total_access_num);
    fprintf(output_file, "Anyway_DRAM_hit rate: %f\n",(double)stat.hostandssdDram_hit_num/stat.total_access_num);
    fprintf(output_file, "Mark page count: %ld\n", mark_count);
    fprintf(output_file, "Evicted marked page count: %ld \n", evict_mark_count);
    fprintf(output_file, "Avg_flash_read_latency: %f \n", (double)stat.total_miss_latency/stat.total_flash_miss_num);
    fprintf(output_file, "#Flash Reads: %ld \n", (uint64_t)stat.total_flash_miss_num);

//     dram_subsystem->the_cache.gen_page_locality_result(main_filename);
    
    return 0;
}
