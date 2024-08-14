/*
 * W25Qxx.h
 *
 *  Created on: Apr 6, 2024
 *      Author: hans6
 */

#ifndef INC_W25QXX_H_
#define INC_W25QXX_H_

// Comment out for some extra debugging info
//#define QSPIDEBUG				1

#define FS_SIZE                 (1024 * 1024 * 8)                   // 8Mbyte **check the same in ios file else -5 error **
#define FS_PAGE_SIZE            256									// Winbond W25Qxx 256 Page program
#define FS_SECTOR_SIZE          4096								// Winbond W25Qxx minimum erase size

#include "lfs_util.h"
#include "lfs.h"
#include "quadspi.h"

struct littlfs_fsstat_t {
    lfs_size_t block_size;
    lfs_size_t block_count;
    lfs_size_t blocks_used;
};


#ifdef QSPIDEBUG
	#define qprintf(...)    printf(__VA_ARGS__)		                // Debug messages on UART0
#else
	#define qprintf(...)
#endif


/*W25Q64JV memory parameters*/
#define MEMORY_FLASH_SIZE				0x800000 /* 64Mbit =>8Mbyte */
#define MEMORY_BLOCK_SIZE				0x10000   /* 128 blocks of 64KBytes */
#define MEMORY_SECTOR_SIZE				0x1000    /* 4kBytes */
#define MEMORY_PAGE_SIZE				0x100     /* 256 bytes */


/*W25Q64JV commands */
#define CHIP_ERASE_CMD 					0xC7
#define READ_STATUS_REG_CMD 			0x05
#define WRITE_ENABLE_CMD 				0x06
#define VOLATILE_SR_WRITE_ENABLE   	 	0x50
#define READ_STATUS_REG2_CMD 			0x35
#define WRITE_STATUS_REG2_CMD 			0x31
#define READ_STATUS_REG3_CMD 			0x15
#define WRITE_STATUS_REG3_CMD       	0x11
#define SECTOR_ERASE_CMD 				0x20
#define BLOCK_ERASE_CMD 				0xD8
#define QUAD_IN_FAST_PROG_CMD 			0x32
#define FAST_PROG_CMD 					0x02
#define QUAD_OUT_FAST_READ_CMD 			0x6B
#define DUMMY_CLOCK_CYCLES_READ_QUAD 	8
#define QUAD_IN_OUT_FAST_READ_CMD 		0xEB
#define RESET_ENABLE_CMD 				0x66
#define RESET_EXECUTE_CMD 				0x99
#define READ_JEDEC_ID_CMD 				0x9F
#define READ_UNIQUE_ID_CMD 				0x4B
#define READ_SFDP_CMD					0x5A


int stmlfs_mount(bool format);
int stmlfs_file_open(lfs_file_t *file, const char *path, int flags);
int stmlfs_file_read(lfs_file_t *file,void *buffer, lfs_size_t size);
int stmlfs_file_rewind(lfs_file_t *file);
lfs_ssize_t stmlfs_file_write(lfs_file_t *file,const void *buffer, lfs_size_t size);
int stmlfs_file_close(lfs_file_t *file);
int stmlfs_unmount(void);
int stmlfs_remove(const char* path);
int stmlfs_rename(const char* oldpath, const char* newpath);
int stmlfs_fflush(lfs_file_t *file);
int stmlfs_dir_open(const char* path);
int stmlfs_dir_close(int dir);
int stmlfs_dir_read(int dir, struct lfs_info* info);
int stmlfs_dir_seek(int dir, lfs_off_t off);
lfs_soff_t stmlfs_dir_tell(int dir);
int stmlfs_dir_rewind(int dir);
lfs_soff_t stmlfs_lseek(lfs_file_t *file, lfs_soff_t off, int whence);
int stmlfs_truncate(lfs_file_t *file, lfs_off_t size);
lfs_soff_t stmlfs_tell(lfs_file_t *file);
int stmlfs_stat(const char* path, struct lfs_info* info);
int stmlfs_fsstat(struct littlfs_fsstat_t* stat);
lfs_ssize_t stmlfs_getattr(const char* path, uint8_t type, void* buffer, lfs_size_t size);
int stmlfs_setattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size);
int stmlfs_removeattr(const char* path, uint8_t type);
int stmlfs_opencfg(lfs_file_t *file, const char* path, int flags, const struct lfs_file_config* config);
lfs_soff_t stmlfs_size(lfs_file_t *file);
int stmlfs_mkdir(const char* path);
const char* stmlfs_errmsg(int err);
void dump_dir(void);


int stmlfs_hal_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size);
int stmlfs_hal_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size);
int stmlfs_hal_erase(const struct lfs_config *c, lfs_block_t sector);
int stmlfs_hal_sync(const struct lfs_config *c);


uint8_t CSP_QUADSPI_Init(void);
uint8_t CSP_QSPI_EraseSector(uint32_t EraseStartAddress ,uint32_t EraseEndAddress);
uint8_t CSP_QSPI_EraseBlock(uint32_t flash_address);
uint8_t CSP_QSPI_WriteMemory(uint8_t* buffer, uint32_t address, uint32_t buffer_size);
uint8_t CSP_QSPI_EnableMemoryMappedMode(void);
uint8_t CSP_QSPI_EnableMemoryMappedMode2(void);
uint8_t CSP_QSPI_Erase_Chip (void);
uint8_t QSPI_AutoPollingMemReady(void);
uint8_t CSP_QSPI_Read(uint8_t* pData, uint32_t ReadAddr, uint32_t Size);
uint8_t QSPI_ReadID(uint32_t *id);
//uint8_t QSPI_ResetChip(void);
uint8_t QSPI_ReadUniqueID(uint8_t *pData);
uint8_t QSPI_ReadSFDP(uint8_t *sfdp);

#endif /* INC_W25QXX_H_ */
