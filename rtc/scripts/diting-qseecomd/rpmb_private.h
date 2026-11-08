// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
// Tiré de github.com/qualcomm/minkipc (listeners/librpmbservice, commit d44965f).

/*
 * rpmb_private.h -- internal state shared between rpmb_emmc.c and rpmb_ufs.c.
 *
 * Not part of the public API.  Do not include from rpmb_service.c.
 */

#ifndef __RPMB_PRIVATE_H__
#define __RPMB_PRIVATE_H__

#include "rpmb.h"

/*
 * Per-device runtime state.
 * fd         -- open file descriptor for the RPMB block/char device (eMMC)
 *              or the RPMB BSG device (UFS).
 * fd_ufs_bsg -- open file descriptor for the UFS BSG query device.
 * init_done  -- set to 1 after successful rpmb_emmc_init / rpmb_ufs_init.
 * info       -- cached device parameters (size, rel_wr_count, dev_type).
 */
struct rpmb_stats {
	int fd;
	int fd_ufs_bsg;
	int init_done;
	rpmb_init_info_t info;
};

extern struct rpmb_stats rpmb;

/*
 * Driver function prototypes -- implemented in rpmb_emmc.c / rpmb_ufs.c,
 * called via the driver table in rpmb.c.
 */

/* eMMC driver */
device_id_type rpmb_emmc_probe(void);
int rpmb_emmc_init(void);
int rpmb_emmc_read(uint32_t *req_buf, uint32_t blk_cnt,
		   uint32_t *resp_buf, uint32_t *resp_len);
int rpmb_emmc_write(uint32_t *req_buf, uint32_t blk_cnt,
		    uint32_t *resp_buf, uint32_t *resp_len,
		    uint32_t frames_per_op);
void rpmb_emmc_exit(void);

/* UFS driver */
device_id_type rpmb_ufs_probe(void);
int rpmb_ufs_init(void);
int rpmb_ufs_read(uint32_t *req_buf, uint32_t blk_cnt,
		  uint32_t *resp_buf, uint32_t *resp_len);
int rpmb_ufs_write(uint32_t *req_buf, uint32_t blk_cnt,
		   uint32_t *resp_buf, uint32_t *resp_len,
		   uint32_t frames_per_op);
void rpmb_ufs_exit(void);

#endif /* __RPMB_PRIVATE_H__ */
