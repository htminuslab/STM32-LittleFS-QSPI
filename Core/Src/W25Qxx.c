/*
 * W25Qxx.c
 *
 *  Created on: Apr 6, 2024
 *      Author: hans6
 */

#include "main.h"
#include "W25Qxx.h"

extern QSPI_HandleTypeDef hqspi;
#define W25Q_SPI hqspi

static lfs_t lfs;													// Littlefs

static uint8_t QSPI_WriteEnable(void);
uint8_t QSPI_AutoPollingMemReady(void);
static uint8_t QSPI_Configuration(void);
static uint8_t QSPI_ResetChip(void);


const struct lfs_config stmconfig = {
    // block device operations
    .read  = stmlfs_hal_read,
    .prog  = stmlfs_hal_prog,
    .erase = stmlfs_hal_erase,
    .sync  = stmlfs_hal_sync,

    // block device configuration
    .read_size      = FS_PAGE_SIZE,
    .prog_size      = FS_PAGE_SIZE,
    .block_size     = FS_SECTOR_SIZE,
    .block_count    = FS_SIZE/FS_SECTOR_SIZE,
    .cache_size     = FS_SECTOR_SIZE/4,
    .lookahead_size = 32,                                           // must be multiple of 8
    .block_cycles   = 100,                                          // 100(better wear levelling)-1000(better performance)
};

int save_and_disable_interrupts(void) {								// Not used
    uint32_t store_primask = __get_PRIMASK();
    __disable_irq();
    return store_primask;
}

void restore_interrupts(int mask) {									// Not used
    __set_PRIMASK(mask);
}

int stmlfs_hal_sync(const struct lfs_config *c)
{
    UNUSED(*c);
    return LFS_ERR_OK;
}

int stmlfs_mount(bool format)
{
	int err=-1;

	assert(FS_SIZE<16777216);										// Chip < 16Mbyte, change R/W to 32bits address

    if (format) {
    	err=lfs_format(&lfs,&stmconfig);
    	printf("lfs_format - returned: %d\n",err);
    }
    err=lfs_mount(&lfs,&stmconfig);                              	// mount the filesystem
    printf("lfs_mount  - returned: %d\n",err);
    return err;
}

int stmlfs_hal_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size)
{
	assert(block < c->block_count);
    assert(off + size <= c->block_size);

    uint32_t p = (block * c->block_size) + off;

    qprintf("stmlfs_hal_read(block=%ld off=%ld size=%ld), addr=0x%08lx\n",block,off,size,p);

    if (CSP_QSPI_Read(buffer, p, size) != HAL_OK) {
    	return LFS_ERR_IO;
    }

    return LFS_ERR_OK;
}

int stmlfs_hal_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size)
{
	assert(block < c->block_count);
	uint32_t p = (block * c->block_size) + off;

    qprintf("stmlfs_hal_prog(block=%ld off=%ld size=%ld), addr=0x%08lx\n",block,off,size,p);

    if (CSP_QSPI_WriteMemory(((uint8_t *)buffer), p, size) != HAL_OK) {
    	return LFS_ERR_IO;
    }

	#ifdef QSPIDEBUG
    uint8_t localbuf[FS_SECTOR_SIZE]={0};
    printf("Read back and compare\n");
    if (CSP_QSPI_Read(localbuf, p, size) != HAL_OK) return LFS_ERR_IO;

    for (int i=0;i<size;i++) {
    	if (localbuf[i]!=((uint8_t *)buffer)[i]) {
    		printf("**** Diff localbuf[%d]=%02x expected %02x\n",i,localbuf[i],((uint8_t *)buffer)[i]);
    	}
    }
	#endif

    return LFS_ERR_OK;
}

int stmlfs_hal_erase(const struct lfs_config *c, lfs_block_t block)
{
	assert(block < c->block_count);
	uint32_t p = block * c->block_size;

    qprintf("stmlfs_hal_erase(block=%ld), start_address=%lx end_address=%lx\n",block,p,p+c->block_size-1);

    if (CSP_QSPI_EraseSector(p,p+c->block_size-1) != HAL_OK){
    	return LFS_ERR_IO;
    }

	#ifdef QSPIDEBUG
	uint8_t localbuf[FS_SECTOR_SIZE]={0};
	printf("Read back and compare to 0xFF\n");
	if (CSP_QSPI_Read(localbuf, p, c->block_size) != HAL_OK) return LFS_ERR_IO;

	for (int i=0;i<c->block_size;i++) {
		if (localbuf[i]!=0xFF) {
			printf("**** Diff localbuf[%d]=%02x expected 0xFF\n",i,localbuf[i]);
		}
	}
	#endif

    return LFS_ERR_OK;
}



int stmlfs_file_open(lfs_file_t *file, const char *path, int flags)
{
    return lfs_file_open(&lfs, file, path, flags);
}

int stmlfs_file_read(lfs_file_t *file,void *buffer, lfs_size_t size)
{
    return lfs_file_read(&lfs, file, buffer, size);
}

int stmlfs_file_rewind(lfs_file_t *file)
{
    return lfs_file_rewind(&lfs, file);
}

lfs_ssize_t stmlfs_file_write(lfs_file_t *file,const void *buffer, lfs_size_t size)
{
    return lfs_file_write(&lfs, file,buffer,size);
}

int stmlfs_file_close(lfs_file_t *file)
{
    return lfs_file_close(&lfs, file);
}

int stmlfs_unmount(void)
{
    return lfs_unmount(&lfs);
}

int stmlfs_remove(const char* path)
{
    return lfs_remove(&lfs, path);
}

int stmlfs_rename(const char* oldpath, const char* newpath)
{
    return lfs_rename(&lfs, oldpath, newpath);
}

int stmlfs_fflush(lfs_file_t *file)
{
    return lfs_file_sync(&lfs, file);
}

int stmlfs_fsstat(struct littlfs_fsstat_t* stat)
{
    stat->block_count = stmconfig.block_count;
    stat->block_size  = stmconfig.block_size;
    stat->blocks_used = lfs_fs_size(&lfs);
    return LFS_ERR_OK;
}



lfs_soff_t stmlfs_lseek(lfs_file_t *file, lfs_soff_t off, int whence)
{
    return lfs_file_seek(&lfs, file, off, whence);
}

int stmlfs_truncate(lfs_file_t *file, lfs_off_t size)
{
    return lfs_file_truncate(&lfs, file, size);
}

lfs_soff_t stmlfs_tell(lfs_file_t *file)
{
    return lfs_file_tell(&lfs, file);
}

int stmlfs_stat(const char* path, struct lfs_info* info)
{
    return lfs_stat(&lfs, path, info);
}

lfs_ssize_t stmlfs_getattr(const char* path, uint8_t type, void* buffer, lfs_size_t size)
{
    return lfs_getattr(&lfs, path, type, buffer, size);
}

int stmlfs_setattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size)
{
    return lfs_setattr(&lfs, path, type, buffer, size);
}

int stmlfs_removeattr(const char* path, uint8_t type)
{
    return lfs_removeattr(&lfs, path, type);
}

int stmlfs_opencfg(lfs_file_t *file, const char* path, int flags, const struct lfs_file_config* config)
{
    return lfs_file_opencfg(&lfs, file, path, flags, config);
}

lfs_soff_t stmlfs_size(lfs_file_t *file)
{
    return lfs_file_size(&lfs, file);
}

int stmlfs_mkdir(const char* path)
{
    return lfs_mkdir(&lfs, path);
}




int stmlfs_dir_open(const char* path)
{
	lfs_dir_t* dir = lfs_malloc(sizeof(lfs_dir_t));
	if (dir == NULL)
		return -1;
	if (lfs_dir_open(&lfs, dir, path) != LFS_ERR_OK) {
		lfs_free(dir);
		return -1;
	}
	return (int)dir;
}

int stmlfs_dir_close(int dir)
{
	return lfs_dir_close(&lfs, (lfs_dir_t*)dir);
	lfs_free((void*)dir);
}

int stmlfs_dir_read(int dir, struct lfs_info* info)
{
    return lfs_dir_read(&lfs, (lfs_dir_t*)dir, info);
}

int stmlfs_dir_seek(int dir, lfs_off_t off)
{
    return lfs_dir_seek(&lfs, (lfs_dir_t*)dir, off);
}

lfs_soff_t stmlfs_dir_tell(int dir)
{
    return lfs_dir_tell(&lfs, (lfs_dir_t*)dir);
}

int stmlfs_dir_rewind(int dir)
{
    return lfs_dir_rewind(&lfs, (lfs_dir_t*)dir);
}

const char* stmlfs_errmsg(int err)
{
    static const struct {
        int err;
        char* text;
    } mesgs[] = {{LFS_ERR_OK, "No error"},
                 {LFS_ERR_IO, "Error during device operation"},
                 {LFS_ERR_CORRUPT, "Corrupted"},
                 {LFS_ERR_NOENT, "No directory entry"},
                 {LFS_ERR_EXIST, "Entry already exists"},
                 {LFS_ERR_NOTDIR, "Entry is not a dir"},
                 {LFS_ERR_ISDIR, "Entry is a dir"},
                 {LFS_ERR_NOTEMPTY, "Dir is not empty"},
                 {LFS_ERR_BADF, "Bad file number"},
                 {LFS_ERR_FBIG, "File too large"},
                 {LFS_ERR_INVAL, "Invalid parameter"},
                 {LFS_ERR_NOSPC, "No space left on device"},
                 {LFS_ERR_NOMEM, "No more memory available"},
                 {LFS_ERR_NOATTR, "No data/attr available"},
                 {LFS_ERR_NAMETOOLONG, "File name too long"}};

    for (unsigned int i = 0; i < sizeof(mesgs) / sizeof(mesgs[0]); i++)
        if (err == mesgs[i].err)
            return mesgs[i].text;
    return "Unknown error";
}


//-------------------------------------------------------------------------------------------------
// display each directory entry name
//-------------------------------------------------------------------------------------------------
void dump_dir(void)
{
    int dir = stmlfs_dir_open("/");
    if (dir < 0) {
    	printf("\nstmlfs_dir_open failed\n");
    	return;
    }

    struct lfs_info info;
    while (stmlfs_dir_read(dir, &info) > 0) {
        printf("%16.16s ", info.name);
        if (info.type==LFS_TYPE_REG) {
            printf(" %04ld\n",info.size);
            // static const char *prefixes[] = {"", "K", "M", "G"};
            // for (int i = sizeof(prefixes)/sizeof(prefixes[0])-1; i >= 0; i--) {
            //     if (info.size >= (1 << 10*i)-1) {
            //         printf("%*u%sB\n", 4-(i != 0), info.size >> 10*i, prefixes[i]);
            //         break;
            //     }
            // }
        } else {
            printf("\n");
        }
    }
    stmlfs_dir_close(dir);

    struct littlfs_fsstat_t stat;                                      // Show file system sizes
    stmlfs_fsstat(&stat);
    printf("\nBlocks %d, block size %d, used %d\n", (int)stat.block_count, (int)stat.block_size,(int)stat.blocks_used);

}

// Software CRC implementation with small lookup table
uint32_t lfs_crc(uint32_t crc, const void* buffer, size_t size) {
    static const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
        0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };

    const uint8_t* data = buffer;

    for (size_t i = 0; i < size; i++) {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }

    return crc;
}

//*************************************************************************************************
// Boring_tech QSPI driver
// https://github.com/osos11-Git/STM32H743VIT6_Boring_TECH_QSPI
//*************************************************************************************************

/* QUADSPI init function */
uint8_t CSP_QUADSPI_Init(void) {

	//prepare QSPI peripheral for ST-Link Utility operations
	hqspi.Instance = QUADSPI;
	if (HAL_QSPI_DeInit(&hqspi) != HAL_OK) {
		return HAL_ERROR;
	}

	MX_QUADSPI_Init();

	if (QSPI_ResetChip() != HAL_OK) {
		return HAL_ERROR;
	}

	HAL_Delay(1);

	if (QSPI_AutoPollingMemReady() != HAL_OK) {
		return HAL_ERROR;
	}

	if (QSPI_WriteEnable() != HAL_OK) {

		return HAL_ERROR;
	}

	if (QSPI_Configuration() != HAL_OK) {
		return HAL_ERROR;
	}

	return HAL_OK;

}

uint8_t CSP_QSPI_Erase_Chip(void) {

	QSPI_CommandTypeDef sCommand;

	if (QSPI_WriteEnable() != HAL_OK) {
		return HAL_ERROR;
	}

	/* Erasing Sequence --------------------------------- */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = CHIP_ERASE_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_NONE;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_NONE;
	sCommand.DummyCycles = 0;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	if (HAL_QSPI_Command(&hqspi, &sCommand, HAL_MAX_DELAY) != HAL_OK) {
		return HAL_ERROR;
	}

	if (QSPI_AutoPollingMemReady() != HAL_OK) {
		return HAL_ERROR;
	}

	return HAL_OK;

}

uint8_t QSPI_AutoPollingMemReady(void) {

	QSPI_CommandTypeDef sCommand = { 0 };
	QSPI_AutoPollingTypeDef sConfig = { 0 };
	HAL_StatusTypeDef ret;

	/* Configure automatic polling mode to wait for memory ready ------ */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = READ_STATUS_REG_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_NONE;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_1_LINE;
	sCommand.DummyCycles = 0;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	sConfig.Match = 0x00;
	sConfig.Mask = 0x01;
	sConfig.MatchMode = QSPI_MATCH_MODE_AND;
	sConfig.StatusBytesSize = 1;
	sConfig.Interval = 0x10;
	sConfig.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;
	if ((ret = HAL_QSPI_AutoPolling(&hqspi, &sCommand, &sConfig,
			HAL_MAX_DELAY)) != HAL_OK) {
		return ret;
	}
	return HAL_OK;
}

static uint8_t QSPI_WriteEnable(void) {
	QSPI_CommandTypeDef sCommand = { 0 };
	QSPI_AutoPollingTypeDef sConfig = { 0 };
	HAL_StatusTypeDef ret;

	/* Enable write operations ------------------------------------------ */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = WRITE_ENABLE_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_NONE;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_NONE;
	sCommand.DummyCycles = 0;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	if ((ret = HAL_QSPI_Command(&hqspi, &sCommand,HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}

	/* Configure automatic polling mode to wait for write enabling ---- */
	sConfig.Match = 0x02;
	sConfig.Mask = 0x02;
	sConfig.MatchMode = QSPI_MATCH_MODE_AND;
	sConfig.StatusBytesSize = 1;
	sConfig.Interval = 0x10;
	sConfig.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

	sCommand.Instruction = READ_STATUS_REG_CMD;
	sCommand.DataMode = QSPI_DATA_1_LINE;

	if ((ret = HAL_QSPI_AutoPolling(&hqspi, &sCommand, &sConfig,HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}
	return HAL_OK;
}


uint8_t QSPI_Configuration(void) {

	QSPI_CommandTypeDef sCommand = { 0 };
	uint8_t reg;
	HAL_StatusTypeDef ret;

	/* Read Volatile Configuration register 2 --------------------------- */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = READ_STATUS_REG2_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_NONE;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_1_LINE;
	sCommand.DummyCycles = 0;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
	sCommand.NbData = 1;

	if ((ret = HAL_QSPI_Command(&hqspi, &sCommand,
			HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}

	if ((ret = HAL_QSPI_Receive(&hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE))
			!= HAL_OK) {
		return ret;
	}

	/* Enable Volatile Write operations ---------------------------------------- */
	sCommand.DataMode = QSPI_DATA_NONE;
	sCommand.Instruction = VOLATILE_SR_WRITE_ENABLE;

	if (HAL_QSPI_Command(&hqspi, &sCommand, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
			!= HAL_OK) {
		return ret;
	}

	/* Write Volatile Configuration register 2 (QE = 1) -- */
	sCommand.DataMode = QSPI_DATA_1_LINE;
	sCommand.Instruction = WRITE_STATUS_REG2_CMD;
	reg |= 2; // QE bit

	if (HAL_QSPI_Command(&hqspi, &sCommand, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
			!= HAL_OK) {
		return ret;
	}

	if (HAL_QSPI_Transmit(&hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
			!= HAL_OK) {
		return ret;
	}

	/* Read Volatile Configuration register 3 --------------------------- */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = READ_STATUS_REG3_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_NONE;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_1_LINE;
	sCommand.DummyCycles = 0;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
	sCommand.NbData = 1;

	if ((ret = HAL_QSPI_Command(&hqspi, &sCommand,
			HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}

	if ((ret = HAL_QSPI_Receive(&hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE))
			!= HAL_OK) {
		return ret;
	}

	/* Write Volatile Configuration register 2 (DRV1:2 = 00) -- */
	sCommand.Instruction = WRITE_STATUS_REG3_CMD;
	reg &= 0x9f; // DRV1:2 bit

	if (HAL_QSPI_Command(&hqspi, &sCommand, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
			!= HAL_OK) {
		return ret;
	}

	if (HAL_QSPI_Transmit(&hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
			!= HAL_OK) {
		return ret;
	}

	return HAL_OK;
}

uint8_t CSP_QSPI_EraseBlock(uint32_t flash_address) { // 64KB
	QSPI_CommandTypeDef sCommand = { 0 };
	QSPI_AutoPollingTypeDef sConfig = { 0 };
	HAL_StatusTypeDef ret;

	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.AddressSize = QSPI_ADDRESS_24_BITS;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	/* Enable write operations ------------------------------------------- */
	if ((ret = QSPI_WriteEnable()) != HAL_OK) {
		return ret;
	}

	/* Erasing Sequence -------------------------------------------------- */
	sCommand.Instruction = BLOCK_ERASE_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_1_LINE;
	sCommand.Address = flash_address;
	sCommand.DataMode = QSPI_DATA_NONE;
	sCommand.DummyCycles = 0;

	if ((ret = HAL_QSPI_Command(&hqspi, &sCommand,HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}


	/* Configure automatic polling mode to wait for Busy to go low ---- */
	sConfig.Match = 0x00;
	sConfig.Mask = 0x01;
	sConfig.MatchMode = QSPI_MATCH_MODE_AND;
	sConfig.StatusBytesSize = 1;
	sConfig.Interval = 0x10;
	sConfig.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;
	sCommand.Instruction = READ_STATUS_REG_CMD;
	sCommand.DataMode = QSPI_DATA_1_LINE;

	if ((ret = HAL_QSPI_AutoPolling(&hqspi, &sCommand, &sConfig,HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}

	return HAL_OK;
}

uint8_t CSP_QSPI_EraseSector(uint32_t EraseStartAddress, uint32_t EraseEndAddress) {

	QSPI_CommandTypeDef sCommand;

	EraseStartAddress = EraseStartAddress
			- EraseStartAddress % MEMORY_SECTOR_SIZE;

	/* Erasing Sequence -------------------------------------------------- */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = SECTOR_ERASE_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_1_LINE;
	sCommand.AddressSize = QSPI_ADDRESS_24_BITS;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	sCommand.DataMode = QSPI_DATA_NONE;
	sCommand.DummyCycles = 0;

	while (EraseEndAddress >= EraseStartAddress) {
		sCommand.Address = (EraseStartAddress & 0x0FFFFFFF);

		if (QSPI_WriteEnable() != HAL_OK) {
			return HAL_ERROR;
		}

		if (HAL_QSPI_Command(&hqspi, &sCommand, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)	!= HAL_OK) {
			return HAL_ERROR;
		}
		EraseStartAddress += MEMORY_SECTOR_SIZE;

		if (QSPI_AutoPollingMemReady() != HAL_OK) {
			return HAL_ERROR;
		}
	}

	return HAL_OK;
}

uint8_t CSP_QSPI_WriteMemory(uint8_t *buffer, uint32_t address,	uint32_t buffer_size) {

	QSPI_CommandTypeDef sCommand;
	uint32_t end_addr, current_size, current_addr;

	/* Calculation of the size between the write address and the end of the page */
	current_addr = 0;

	//
	while (current_addr <= address) {
		current_addr += MEMORY_PAGE_SIZE;
	}
	current_size = current_addr - address;

	/* Check if the size of the data is less than the remaining place in the page */
	if (current_size > buffer_size) {
		current_size = buffer_size;
	}

	/* Initialize the adress variables */
	current_addr = address;
	end_addr = address + buffer_size;

	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = QUAD_IN_FAST_PROG_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_1_LINE;
	sCommand.AddressSize = QSPI_ADDRESS_24_BITS;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
	sCommand.DataMode = QSPI_DATA_4_LINES;
	sCommand.NbData = buffer_size;
	sCommand.Address = address;
	sCommand.DummyCycles = 0;

	/* Perform the write page by page */
	do {
		sCommand.Address = current_addr;
		sCommand.NbData = current_size;

		if (current_size == 0) return HAL_OK;


		/* Enable write operations */
		if (QSPI_WriteEnable() != HAL_OK) return HAL_ERROR;


		/* Configure the command */
		if (HAL_QSPI_Command(&hqspi, &sCommand, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)!= HAL_OK) {
			return HAL_ERROR;
		}

		/* Transmission of the data */
		if (HAL_QSPI_Transmit(&hqspi, buffer, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)!= HAL_OK) {
			return HAL_ERROR;
		}

		/* Configure automatic polling mode to wait for end of program */
		if (QSPI_AutoPollingMemReady() != HAL_OK) {
			return HAL_ERROR;
		}

		/* Update the address and size variables for next page programming */
		current_addr += current_size;
		buffer += current_size;
		current_size = ((current_addr + MEMORY_PAGE_SIZE) > end_addr) ?
						(end_addr - current_addr) : MEMORY_PAGE_SIZE;
	} while (current_addr <= end_addr);

	return HAL_OK;

}

uint8_t CSP_QSPI_EnableMemoryMappedMode(void) {

	QSPI_CommandTypeDef sCommand;
	QSPI_MemoryMappedTypeDef sMemMappedCfg;

	/* Enable Memory-Mapped mode-------------------------------------------------- */

	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = QUAD_OUT_FAST_READ_CMD;
	sCommand.AddressSize = QSPI_ADDRESS_24_BITS;
	sCommand.AddressMode = QSPI_ADDRESS_1_LINE;
	sCommand.Address = 0;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
	sCommand.DataMode = QSPI_DATA_4_LINES;
	sCommand.NbData = 0;
	sCommand.DummyCycles = 8;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.AlternateBytes = 0;
	sCommand.AlternateBytesSize = 0;

	sMemMappedCfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
	sMemMappedCfg.TimeOutPeriod = 0;

	if (HAL_QSPI_MemoryMapped(&hqspi, &sCommand, &sMemMappedCfg) != HAL_OK) {
		return HAL_ERROR;
	}
	return HAL_OK;
}

uint8_t CSP_QSPI_EnableMemoryMappedMode2(void) {

	QSPI_CommandTypeDef sCommand;
	QSPI_MemoryMappedTypeDef sMemMappedCfg;

	/* Enable Memory-Mapped mode-------------------------------------------------- */

	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = QUAD_IN_OUT_FAST_READ_CMD;
	sCommand.AddressSize = QSPI_ADDRESS_24_BITS;
	sCommand.AddressMode = QSPI_ADDRESS_4_LINES;
	sCommand.Address = 0;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
	sCommand.AlternateBytes = 0xFF;
	sCommand.AlternateBytesSize = 1;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
	sCommand.DataMode = QSPI_DATA_4_LINES;
	sCommand.NbData = 0;
	sCommand.DummyCycles = 4;

	sMemMappedCfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
	sMemMappedCfg.TimeOutPeriod = 0;

	if (HAL_QSPI_MemoryMapped(&hqspi, &sCommand, &sMemMappedCfg) != HAL_OK) {
		return HAL_ERROR;
	}
	return HAL_OK;
}

uint8_t QSPI_ResetChip(void) {
	QSPI_CommandTypeDef sCommand = { 0 };
	uint32_t temp = 0;
	HAL_StatusTypeDef ret;

	/* Enable Reset --------------------------- */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = RESET_ENABLE_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_NONE;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_NONE;
	sCommand.DummyCycles = 0;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	if ((ret = HAL_QSPI_Command(&hqspi, &sCommand,
			HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}

	/* Reset Device --------------------------- */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = RESET_EXECUTE_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_NONE;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_NONE;
	sCommand.DummyCycles = 0;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	if ((ret = HAL_QSPI_Command(&hqspi, &sCommand,
			HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}

	for (temp = 0; temp < 500000; temp++) {
		__NOP();
	}

	return HAL_OK;
}


uint8_t QSPI_ReadID(uint32_t *id) {
	QSPI_CommandTypeDef sCommand = { 0 };
	uint8_t pData[3]={0};
	HAL_StatusTypeDef ret;

	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = READ_JEDEC_ID_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_NONE;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_1_LINE;
	sCommand.DummyCycles = 0;
	sCommand.NbData = 3;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	if ((ret = HAL_QSPI_Command(&hqspi, &sCommand,HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}

	if (HAL_QSPI_Receive(&hqspi, pData, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)	!= HAL_OK) {
		return HAL_ERROR;
	}

	*id=((uint32_t)pData[0]<<16)|((uint32_t)pData[1]<<8)|(uint32_t)pData[2];

	return HAL_OK;
}

uint8_t QSPI_ReadUniqueID(uint8_t *pData)
{
	QSPI_CommandTypeDef sCommand = { 0 };
	HAL_StatusTypeDef ret;

	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = READ_UNIQUE_ID_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_1_LINE;
	sCommand.AddressSize = QSPI_ADDRESS_32_BITS;
	sCommand.Address = 0;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_1_LINE;
	sCommand.DummyCycles = 0;
	sCommand.NbData = 8;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	if ((ret = HAL_QSPI_Command(&hqspi, &sCommand,HAL_QPSI_TIMEOUT_DEFAULT_VALUE)) != HAL_OK) {
		return ret;
	}

	if (HAL_QSPI_Receive(&hqspi, pData, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)	!= HAL_OK) {
		return HAL_ERROR;
	}

	return HAL_OK;
}


uint8_t CSP_QSPI_Read(uint8_t *pData, uint32_t ReadAddr, uint32_t Size) {
	QSPI_CommandTypeDef sCommand;


	qprintf(" CSP_QSPI_Read(0x%lx,%d)\n",ReadAddr,Size);

	/* Initialize the read command */
	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = QUAD_IN_OUT_FAST_READ_CMD;
	sCommand.AddressMode = QSPI_ADDRESS_4_LINES;
	sCommand.AddressSize = QSPI_ADDRESS_24_BITS;
	sCommand.Address = ReadAddr;
	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_4_LINES;
	sCommand.DummyCycles = 6U;
	sCommand.NbData = Size;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	/* Configure the command */
	if (HAL_QSPI_Command(&hqspi, &sCommand, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)!= HAL_OK) {
		return HAL_ERROR;
	}

	/* Set S# timing for Read command */
	MODIFY_REG(hqspi.Instance->DCR, QUADSPI_DCR_CSHT, QSPI_CS_HIGH_TIME_5_CYCLE);

	/* Reception of the data */
	if (HAL_QSPI_Receive(&hqspi, pData, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)!= HAL_OK) {
		return HAL_ERROR;
	}

	/* Restore S# timing for nonRead commands */
	MODIFY_REG(hqspi.Instance->DCR, QUADSPI_DCR_CSHT,QSPI_CS_HIGH_TIME_6_CYCLE);

	return HAL_OK;
}


uint8_t QSPI_ReadSFDP(uint8_t *sfdp)
{
	QSPI_CommandTypeDef sCommand;

	sCommand.InstructionMode = QSPI_INSTRUCTION_1_LINE;
	sCommand.Instruction = READ_SFDP_CMD;

	sCommand.AddressMode = QSPI_ADDRESS_1_LINE;						// 4 Dummy bytes
	sCommand.AddressSize = QSPI_ADDRESS_32_BITS;
	sCommand.Address = 0;

	sCommand.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	sCommand.DataMode = QSPI_DATA_1_LINE;
	sCommand.DummyCycles = 0;
	sCommand.NbData = 256;
	sCommand.DdrMode = QSPI_DDR_MODE_DISABLE;
	sCommand.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
	sCommand.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

	/* Configure the command */
	if (HAL_QSPI_Command(&hqspi, &sCommand, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)!= HAL_OK) {
		return HAL_ERROR;
	}

	/* Set S# timing for Read command */
	MODIFY_REG(hqspi.Instance->DCR, QUADSPI_DCR_CSHT,QSPI_CS_HIGH_TIME_5_CYCLE);

	/* Reception of the data */
	if (HAL_QSPI_Receive(&hqspi, sfdp, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)!= HAL_OK) {
		return HAL_ERROR;
	}

	/* Restore S# timing for nonRead commands */
	MODIFY_REG(hqspi.Instance->DCR, QUADSPI_DCR_CSHT,QSPI_CS_HIGH_TIME_6_CYCLE);

	return HAL_OK;
}

