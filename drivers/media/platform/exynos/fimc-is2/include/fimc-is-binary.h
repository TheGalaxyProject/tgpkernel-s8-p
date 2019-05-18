/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_BINARY_H
#define FIMC_IS_BINARY_H

#include "fimc-is-config.h"

#if defined(CONFIG_EXYNOS_ASB)
#define FIMC_IS_FW_PATH                        "/system/vendor/firmware/"
#define FIMC_IS_FW_DUMP_PATH                   "/data/"
#define FIMC_IS_SETFILE_SDCARD_PATH            "/data/"
#define FIMC_IS_FW_SDCARD                      "/data/fimc_is_fw2.bin"
#define FIMC_IS_FW                             "fimc_is_fw2.bin"
#define FIMC_IS_ISP_LIB_SDCARD_PATH            "./data/"

#else
#ifdef VENDER_PATH
#define FIMC_IS_FW_PATH 			"/system/vendor/firmware/"
#define FIMC_IS_FW_DUMP_PATH			"/data/camera/"
#define FIMC_IS_SETFILE_SDCARD_PATH		"/data/media/0/"
#define FIMC_IS_FW_SDCARD			"/data/media/0/fimc_is_fw.bin"
#define FIMC_IS_FW				"fimc_is_fw.bin"
#define FIMC_IS_LIB_PATH			"/system/vendor/firmware/"
#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
#define FIMC_IS_ISP_LIB_SDCARD_PATH        	NULL
#else
#define FIMC_IS_ISP_LIB_SDCARD_PATH        	"/data/media/0/"
#endif
#define FIMC_IS_REAR_CAL_SDCARD_PATH		"/data/media/0/"
#define FIMC_IS_FRONT_CAL_SDCARD_PATH		"/data/media/0/"
#else
#define FIMC_IS_FW_PATH 			"/system/vendor/firmware/"
#define FIMC_IS_FW_DUMP_PATH			"/data/"
#define FIMC_IS_SETFILE_SDCARD_PATH		"/data/"
#define FIMC_IS_FW_SDCARD			"/data/fimc_is_fw2.bin"
#define FIMC_IS_FW				"fimc_is_fw2.bin"
#define FIMC_IS_ISP_LIB_SDCARD_PATH		"/data/"
#define FIMC_IS_REAR_CAL_SDCARD_PATH		"/data/"
#define FIMC_IS_FRONT_CAL_SDCARD_PATH		"/data/"
#endif
#endif /* defined(CONFIG_EXYNOS_ASB) */

#ifdef USE_ONE_BINARY
#define FIMC_IS_ISP_LIB				"fimc_is_lib.bin"
#else
#define FIMC_IS_ISP_LIB				"fimc_is_lib_isp.bin"
#define FIMC_IS_VRA_LIB				"fimc_is_lib_vra.bin"
#endif

#define FIMC_IS_RTA_LIB				"fimc_is_rta.bin"

#define FD_SW_BIN_NAME				"fimc_is_fd.bin"
#define FD_SW_SDCARD				"/data/fimc_is_fd.bin"

#ifdef ENABLE_IS_CORE
#define FW_MEM_SIZE			0x02000000
#define FW_BACKUP_SIZE			0x02000000
#define PARAM_REGION_SIZE		0x00005000
#define SHARED_OFFSET			0x01FC0000
#define SHARED_SIZE			0x00010000
#define TTB_OFFSET			0x01BF8000
#define TTB_SIZE			0x00008000
#define DEBUG_REGION_OFFSET		0x01F40000
#define DEBUG_REGION_SIZE		0x0007D000 /* 500KB */
#define CAL_OFFSET0			0x01FD0000
#define CAL_OFFSET1			0x01FE0000

#define DEBUGCTL_OFFSET			(DEBUG_REGION_OFFSET + DEBUG_REGION_SIZE)
#else /* #ifdef ENABLE_IS_CORE */
/* static reserved memory for libraries */
#define LIB_OFFSET		(VMALLOC_START + 0xF6000000 - 0x8000000)
#define LIB_START		(LIB_OFFSET + 0x04000000)

#define VRA_LIB_ADDR		(LIB_START)
#define VRA_LIB_SIZE		(SZ_512K)

#define DDK_LIB_ADDR		(LIB_START + VRA_LIB_SIZE)
#define DDK_LIB_SIZE		(SZ_4M)

#define RTA_LIB_ADDR		(LIB_START + VRA_LIB_SIZE + DDK_LIB_SIZE)
#define RTA_LIB_SIZE		(SZ_1M + SZ_2M)

#ifdef USE_RTA_BINARY
#define LIB_SIZE		(VRA_LIB_SIZE + DDK_LIB_SIZE +  RTA_LIB_SIZE)
#else
#define LIB_SIZE		(VRA_LIB_SIZE + DDK_LIB_SIZE)
#endif

/* reserved memory for FIMC-IS */
#define SETFILE_SIZE		(0x002AF000)
#define REAR_CALDATA_SIZE	(0x00010000)
#define FRONT_CALDATA_SIZE	(0x00010000)
#define DEBUG_REGION_SIZE	(0x0007D000)
#define FSHARED_REGION_SIZE	(0x00010000)
#define DATA_REGION_SIZE	(0x00010000)
#define PARAM_REGION_SIZE	(0x00005000)	/* 20KB * instance(4) */

#define RESERVE_LIB_SIZE	(FIMC_IS_RESERVE_LIB_SIZE)	/* 2MB */
#define TAAISP_DMA_SIZE		(FIMC_IS_TAAISP_SIZE)	/* 512KB */
#define LHFD_MAP_SIZE		(0x009F0000)	/* 9.9375MB */
#define VRA_DMA_SIZE		(FIMC_IS_VRA_SIZE)	/* 8MB */

/* for compatibility */
#define DEBUGCTL_OFFSET		(0)
#define DEBUG_REGION_OFFSET	(0)
#define DEBUG_REGION_SIZE	(0x0007D000) /* 500KB */
#define TTB_OFFSET		(0)
#define TTB_SIZE		(0)
#define CAL_OFFSET0		(0x01FD0000)
#define CAL_OFFSET1		(0x01FE0000)

/* EEPROM offset */
#define EEPROM_HEADER_BASE	(0)
#define EEPROM_OEM_BASE		(0x100)
#define EEPROM_AWB_BASE		(0x200)
#define EEPROM_SHADING_BASE	(0x300)
#define EEPROM_PDAF_BASE	(0x0)

/* FROM offset */
#define FROM_HEADER_BASE	(0)
#define FROM_OEM_BASE		(0x1000)
#define FROM_AWB_BASE		(0x2000)
#define FROM_SHADING_BASE	(0x3000)
#define FROM_PDAF_BASE		(0x5000)
#endif

#define FIMC_IS_FW_BASE_MASK			((1 << 26) - 1)
#define FIMC_IS_VERSION_SIZE			42
#define FIMC_IS_VERSION_BIN_SIZE		44
#ifdef USE_BINARY_PADDING_DATA_ADDED
#define FIMC_IS_SIGNATURE_SIZE  		272
#define FIMC_IS_VERSION_OFFSET			(FIMC_IS_VERSION_BIN_SIZE+FIMC_IS_SIGNATURE_SIZE)
#else
#define FIMC_IS_VERSION_OFFSET			(FIMC_IS_VERSION_BIN_SIZE)
#endif
#define FIMC_IS_SETFILE_VER_OFFSET		0x40
#define FIMC_IS_SETFILE_VER_SIZE		52

#define FIMC_IS_REAR_CAL			"rear_cal_data.bin"
#define FIMC_IS_FRONT_CAL			"front_cal_data.bin"
#define FIMC_IS_CAL_SDCARD			"/data/cal_data.bin"
#define FIMC_IS_CAL_RETRY_CNT			(2)
#define FIMC_IS_FW_RETRY_CNT			(2)
#define FIMC_IS_CAL_VER_SIZE			(12)

enum fimc_is_bin_type {
	FIMC_IS_BIN_FW = 0,
	FIMC_IS_BIN_SETFILE,
	FIMC_IS_BIN_DDK_LIBRARY,
	FIMC_IS_BIN_RTA_LIBRARY,
	FIMC_IS_BIN_COMPANION,
};

struct fimc_is_binary {
	void *data;
	size_t size;

	const struct firmware *fw;

	unsigned long customized;

	/* request_firmware retry */
	unsigned int retry_cnt;
	int	retry_err;

	/* custom memory allocation */
	void *(*alloc)(unsigned long size);
	void (*free)(const void *buf);
};

ssize_t write_data_to_file(char *name, char *buf, size_t count, loff_t *pos);
int get_filesystem_binary(const char *filename, struct fimc_is_binary *bin);
int put_filesystem_binary(const char *filename, struct fimc_is_binary *bin, u32 flags);
void setup_binary_loader(struct fimc_is_binary *bin,
				unsigned int retry_cnt, int retry_err,
				void *(*alloc)(unsigned long size),
				void (*free)(const void *buf));
int request_binary(struct fimc_is_binary *bin, const char *path,
				const char *name, struct device *device);
void release_binary(struct fimc_is_binary *bin);
int was_loaded_by(struct fimc_is_binary *bin);

#endif
