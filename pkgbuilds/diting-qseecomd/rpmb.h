// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
// Tiré de github.com/qualcomm/minkipc (listeners/librpmbservice, commit d44965f).

#ifndef __RPMB_H__
#define __RPMB_H__

#include <stdint.h>
#include <sys/types.h>

/*
 * Wire-protocol constants (JEDEC eMMC 4.5 / UFS 2.0)
 */

#define RPMB_SECTOR_SIZE 256 /* Data payload per RPMB half-sector */
#define RPMB_BLK_SIZE 512 /* Full RPMB frame size on the wire */
#define RPMB_MIN_BLK_CNT 1 /* Minimum frames per ioctl */

/*
 * Storage device type (must match secure-world definitions)
 */

typedef enum {
	EMMC_USER = 0, /* User Partition in eMMC */
	EMMC_BOOT1, /* Boot1 Partition in eMMC */
	EMMC_BOOT0, /* Boot2 Partition in eMMC */
	EMMC_RPMB, /* RPMB Partition in eMMC */
	EMMC_GPP1, /* GPP1 Partition in eMMC */
	EMMC_GPP2, /* GPP2 Partition in eMMC */
	EMMC_GPP3, /* GPP3 Partition in eMMC */
	EMMC_GPP4, /* GPP4 Partition in eMMC */
	EMMC_ALL, /* Entire eMMC device */
	UFS_RPMB, /* RPMB Partition in UFS device */
	UFS_ALL, /* Entire UFS device */
	NO_DEVICE = 0x7FFFFFFF
} device_id_type;

/*
 * RPMB frame request/response codes (JEDEC)
 */

enum rpmb_request_type {
	KEY_PROVISION = 0x01,
	READ_WRITE_COUNTER = 0x02,
	AUTH_WRITE = 0x03,
	AUTH_READ = 0x04,
	READ_RESULT_REG = 0x05,
};

enum rpmb_result_code {
	OPERATION_OK = 0x00,
	GENERAL_FAILURE = 0x01,
	AUTH_FAILURE = 0x02,
	COUNTER_FAILURE = 0x03,
	ADDRESS_FAILURE = 0x04,
	WRITE_FAILURE = 0x05,
	READ_FAILURE = 0x06,
	KEY_NOT_PROG = 0x07,
	MAXED_WR_COUNTER = 0x80,
};

/*
 * RPMB frame layout (JEDEC eMMC 4.5 / UFS 2.0, big-endian fields)
 */

struct rpmb_frame {
	uint8_t stuff_bytes[196]; /* Padding */
	uint8_t key_mac[32]; /* Key / MAC */
	uint8_t data[256]; /* Data payload */
	uint8_t nonce[16]; /* Nonce */
	uint8_t write_counter[4]; /* Write counter (big-endian) */
	uint8_t address[2]; /* Half-sector address (big-endian) */
	uint8_t block_count[2]; /* Block count (big-endian) */
	uint8_t result[2]; /* Result code (big-endian) */
	uint8_t request_response[2]; /* Request / Response type (big-endian) */
};

/* Pre-built result-register read request frame */
extern const struct rpmb_frame read_result_reg_frame;

/*
 * Device init info (exchanged with secure world)
 */

typedef struct {
	uint32_t size; /* RPMB partition size in 512-byte sectors */
	uint32_t rel_wr_count; /* Reliable-write frame count per operation */
	uint32_t dev_type; /* device_id_type of the detected device */
	uint32_t reserved;
} rpmb_init_info_t;

/*
 * Device operations -- one entry per storage driver (eMMC, UFS, ...)
 *
 * rpmb.c holds a table of these and selects the first whose probe()
 * succeeds at service startup.  Adding a new storage type requires only
 * a new row in that table; no other code changes.
 */

typedef struct {
	const char *name;
	device_id_type dev_type;

	/* probe -- return dev_type if device is present, NO_DEVICE otherwise */
	device_id_type (*probe)(void);

	/* init -- open device, populate rpmb.info; return 0 on success */
	int (*init)(void);

	/* read -- authenticated read of blk_cnt frames */
	int (*read)(uint32_t *req_buf, uint32_t blk_cnt,
		    uint32_t *resp_buf, uint32_t *resp_len);

	/*
	 * write -- authenticated write of blk_cnt frames.
	 * frames_per_op: MAC batch size from secure world; all frames in one
	 *                batch share a single HMAC and write counter.
	 */
	int (*write)(uint32_t *req_buf, uint32_t blk_cnt,
		     uint32_t *resp_buf, uint32_t *resp_len,
		     uint32_t frames_per_op);

	/* exit -- release device resources */
	void (*exit)(void);
} rpmb_dev_ops_t;

/*
 * Public API (implemented in rpmb.c)
 */

/* rpmb_init - detect storage device and initialise RPMB subsystem */
int rpmb_init(rpmb_init_info_t *info);

/* rpmb_read - authenticated read via the active storage device */
int rpmb_read(uint32_t *req_buf, uint32_t blk_cnt,
	      uint32_t *resp_buf, uint32_t *resp_len);

/* rpmb_write - authenticated write; frames_per_op is the MAC batch size */
int rpmb_write(uint32_t *req_buf, uint32_t blk_cnt,
	       uint32_t *resp_buf, uint32_t *resp_len,
	       uint32_t frames_per_op);

/*
 * Wakelock helpers (shared by eMMC and UFS drivers, implemented in rpmb.c)
 */

void rpmb_init_wakelock(void);
void rpmb_wakelock(void);
void rpmb_wakeunlock(void);

#endif /* __RPMB_H__ */
