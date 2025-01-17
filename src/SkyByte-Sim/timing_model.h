#ifndef __BYTEFS_TIMING_H__
#define __BYTEFS_TIMING_H__

#define SSD_DRAM_WRITE_CACHELINE_LATENCY (80)

#define NAND_READ_LATENCY  (3000)
#define NAND_PROG_LATENCY  (80000)
#define NAND_BLOCK_ERASE_LATENCY  (1000000)



#define CHNL_TRANSFER_LATENCY_NS (20000)
// #define DMA_TRANSFER_LATENCY_BLOCK ()


//memory interface
#define PCIE_RC_TRANSFER_LATENCY (500) 
#define HOST_RC_TRANSFER_LATENCY (500)


//estimated latency for each io interface:
#define BLOKC_IO_READ_CACHED            (CHNL_TRANSFER_LATENCY_NS)
#define BLOCK_IO_WRITE_CACHED           (CHNL_TRANSFER_LATENCY_NS)
#define BLOKC_IO_READ_NOT_CACHED        (NAND_READ_LATENCY + CHNL_TRANSFER_LATENCY_NS)
#define BLOCK_IO_WRITE_NOT_CACHED       (CHNL_TRANSFER_LATENCY_NS)

#define MEM_IO_READ_CACHED              (PCIE_RC_TRANSFER_LATENCY + HOST_RC_TRANSFER_LATENCY)
#define MEM_IO_WRITE_CACHED             (PCIE_RC_TRANSFER_LATENCY)
#define MEM_IO_READ_NOT_CACHED          (PCIE_RC_TRANSFER_LATENCY + HOST_RC_TRANSFER_LATENCY + NAND_READ_LATENCY)
#define MEM_IO_WRITE_NOT_CACHED         (PCIE_RC_TRANSFER_LATENCY)
#endif
