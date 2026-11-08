// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
// Tiré de github.com/qualcomm/minkipc (listeners/librpmbservice, commit d44965f).
// diting : garde-fous ajoutés (taille de transfert RPMB nulle, nombre de
// trames par opération nul).

#define LOG_TAG "rpmb_ufs"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "rpmb.h"
#include "rpmb_logging.h"
#include "rpmb_private.h"
#include "rpmb_ufs.h"

/* UFS BSG device node path -- populated by get_ufs_bsg_dev() at init */
static char ufs_bsg_dev[FNAME_SZ] = "/dev/bsg/ufs-bsg0";

/* RPMB BSG device node path -- populated by get_rpmb_bsg_dev() at init */
static char rpmb_bsg_dev[FNAME_SZ] = "/dev/bsg/0:0:0:49476";

static int get_ufs_bsg_dev(void)
{
	DIR *dir;
	struct dirent *ent;
	int ret = -ENODEV;

	/* Try /dev/bsg first (unified approach) */
	dir = opendir("/dev/bsg");
	if (dir != NULL) {
		/* read all the files and directories within directory */
		while ((ent = readdir(dir)) != NULL) {
			if (!strcmp(ent->d_name, "ufs-bsg") ||
			    !strcmp(ent->d_name, "ufs-bsg0")) {
				strncpy(ufs_bsg_dev, "/dev/bsg/", FNAME_SZ - 1);
				strncat(ufs_bsg_dev, ent->d_name, FNAME_SZ - strlen(ufs_bsg_dev) - 1);
				ufs_bsg_dev[FNAME_SZ - 1] = '\0';
				ret = 0;
				break;
			}
		}
		closedir(dir);
	}

	/* Fallback to /dev if not found in /dev/bsg */
	if (ret != 0) {
		dir = opendir("/dev");
		if (dir != NULL) {
			while ((ent = readdir(dir)) != NULL) {
				if (!strcmp(ent->d_name, "ufs-bsg") ||
				    !strcmp(ent->d_name, "ufs-bsg0")) {
					strncpy(ufs_bsg_dev, "/dev/", FNAME_SZ - 1);
					strncat(ufs_bsg_dev, ent->d_name, FNAME_SZ - strlen(ufs_bsg_dev) - 1);
					ufs_bsg_dev[FNAME_SZ - 1] = '\0';
					ret = 0;
					break;
				}
			}
			closedir(dir);
		} else {
			RPMB_LOG_ERROR("could not open /dev or /dev/bsg (error no: %d)\n", errno);
			ret = -EINVAL;
		}
	}

	if (ret)
		RPMB_LOG_ERROR("could not find the ufs-bsg dev\n");

	return ret;
}

/*
 * get_rpmb_bsg_dev - scan /dev/bsg for the UFS RPMB well-known LUN node.
 *
 * The kernel names the BSG node H:C:T:L where L is the decimal LUN.
 * UPIU_RPMB_LUN (0xC4) is exposed as decimal 49476 in the BSG path.
 * We scan for any entry ending in ":49476" rather than hardcoding the
 * host/channel/target numbers, which vary by platform.
 */
static int get_rpmb_bsg_dev(void)
{
	DIR *dir;
	struct dirent *ent;
	size_t len;
	int ret = -ENODEV;

	dir = opendir("/dev/bsg");
	if (!dir) {
		RPMB_LOG_ERROR("could not open /dev/bsg (error no: %d)\n",
			       errno);
		return -EINVAL;
	}

	while ((ent = readdir(dir)) != NULL) {
		len = strlen(ent->d_name);
		/* Match any H:C:T:49476 entry */
		if (len > 6 && strcmp(ent->d_name + len - 6, ":49476") == 0) {
			/* d_name is NAME_MAX (255); cap to what fits in rpmb_bsg_dev */
			snprintf(rpmb_bsg_dev, sizeof(rpmb_bsg_dev),
				 "/dev/bsg/%.54s", ent->d_name);
			RPMB_LOG_INFO("Found RPMB BSG device: %s\n",
				      rpmb_bsg_dev);
			ret = 0;
			break;
		}
	}
	closedir(dir);

	if (ret)
		RPMB_LOG_ERROR("could not find RPMB BSG device\n");

	return ret;
}

static int ufs_bsg_dev_open(void)
{
	if (rpmb.fd_ufs_bsg < 0) {
		rpmb.fd_ufs_bsg = open(ufs_bsg_dev, O_RDWR);
		if (rpmb.fd_ufs_bsg < 0) {
			RPMB_LOG_ERROR("Unable to open %s (error no: %d)\n",
				       ufs_bsg_dev, errno);
			return -errno;
		}
	}
	return 0;
}

static void ufs_bsg_dev_close(void)
{
	if (rpmb.fd_ufs_bsg >= 0) {
		close(rpmb.fd_ufs_bsg);
		rpmb.fd_ufs_bsg = -1;
	}
}

static int rpmb_bsg_dev_open(void)
{
	if (rpmb.fd < 0) {
		rpmb.fd = open(rpmb_bsg_dev, O_RDWR | O_SYNC);
		if (rpmb.fd < 0) {
			RPMB_LOG_ERROR("Unable to open %s (error no: %d)\n",
				       rpmb_bsg_dev, errno);
			return -errno;
		}
	}
	return 0;
}

static void rpmb_bsg_dev_close(void)
{
	if (rpmb.fd >= 0) {
		close(rpmb.fd);
		rpmb.fd = -1;
	}
}

static int ufs_bsg_ioctl(int fd, struct ufs_bsg_request *req,
			 struct ufs_bsg_reply *rsp, __u8 *buf, __u32 buf_len,
			 enum bsg_ioctl_dir dir)
{
	int ret;
	struct sg_io_v4 sg_io = {0};

	sg_io.guard = 'Q';
	sg_io.protocol = BSG_PROTOCOL_SCSI;
	sg_io.subprotocol = BSG_SUB_PROTOCOL_SCSI_TRANSPORT;
	sg_io.request_len = sizeof(*req);
	sg_io.request = (__u64)(uintptr_t)req;
	sg_io.response = (__u64)(uintptr_t)rsp;
	sg_io.max_response_len = sizeof(*rsp);
	if (dir == BSG_IOCTL_DIR_FROM_DEV) {
		sg_io.din_xfer_len = buf_len;
		sg_io.din_xferp = (__u64)(uintptr_t)(buf);
	} else {
		sg_io.dout_xfer_len = buf_len;
		sg_io.dout_xferp = (__u64)(uintptr_t)(buf);
	}

	ret = ioctl(fd, SG_IO, &sg_io);
	if (ret)
		RPMB_LOG_ERROR("%s: Error from sg_io ioctl (return value: %d, error no: %d, reply result from LLD: %d)\n",
		     __func__, ret, errno, rsp->result);

	if (sg_io.info || rsp->result) {
		RPMB_LOG_ERROR("%s: Error from sg_io info (check sg info: device_status: 0x%x, transport_status: 0x%x, driver_status: 0x%x, reply result from LLD: %d)\n",
		     __func__, sg_io.device_status, sg_io.transport_status,
		     sg_io.driver_status, rsp->result);
		ret = -EIO;
	}

	return ret;
}

static void compose_ufs_bsg_query_req(struct ufs_bsg_request *req, __u8 func,
				    __u8 opcode, __u8 idn, __u8 index, __u8 sel,
				    __u16 length)
{
	struct utp_upiu_header *hdr = &req->upiu_req.header;
	struct utp_upiu_query *qr = &req->upiu_req.qr;

	req->msgcode = UTP_UPIU_QUERY_REQ;
	hdr->dword_0 = DWORD(UTP_UPIU_QUERY_REQ, 0, 0, 0);
	hdr->dword_1 = DWORD(0, func, 0, 0);
	hdr->dword_2 = DWORD(0, 0, length >> 8, (__u8)length);
	qr->opcode = opcode;
	qr->idn = idn;
	qr->index = index;
	qr->selector = sel;
	qr->length = htobe16(length);
}

static int ufs_query_desc(int fd, __u8 *buf,
			  __u16 buf_len, __u8 func, __u8 opcode, __u8 idn,
			  __u8 index, __u8 sel)
{
	struct ufs_bsg_request req = {0};
	struct ufs_bsg_reply rsp = {0};
	enum bsg_ioctl_dir dir = BSG_IOCTL_DIR_FROM_DEV;
	int ret = 0;

	if (opcode == QUERY_REQ_OP_WRITE_DESC)
		dir = BSG_IOCTL_DIR_TO_DEV;

	compose_ufs_bsg_query_req(&req, func, opcode, idn, index, sel, buf_len);

	ret = ufs_bsg_ioctl(fd, &req, &rsp, buf, buf_len, dir);
	if (ret)
		RPMB_LOG_ERROR("%s: Error from ufs_bsg_ioctl (return value: %d, error no: %d)\n",
		     __func__, ret, errno);

	return ret;
}

static int ufs_read_desc(int fd, __u8 *buf, __u16 buf_len,
			 __u8 idn, __u8 index)
{
	return ufs_query_desc(fd, buf, buf_len, QUERY_REQ_FUNC_STD_READ,
			      QUERY_REQ_OP_READ_DESC, idn, index, 0);
}

static int32_t get_ufs_rpmb_parameters(void)
{
	__u8 device_data[QUERY_DESC_SIZE_DEVICE] = {0};
	__u8 geo_data[QUERY_DESC_SIZE_GEOMETRY] = {0};
	__u8 unit_data[QUERY_DESC_SIZE_UNIT] = {0};
	uint16_t wspecversion = 0;
	uint32_t rpmb_num_blocks = 0;
	int ret;

	ret = ufs_bsg_dev_open();
	if (ret)
		return ret;

	ret = ufs_read_desc(rpmb.fd_ufs_bsg, device_data,
			    QUERY_DESC_SIZE_DEVICE,
			    QUERY_DESC_IDN_DEVICE, 0);
	if (ret) {
		RPMB_LOG_ERROR("Error requesting ufs device info via query ioctl (return value: %d, error no: %d)\n",
				ret, errno);
		goto out;
	}

	wspecversion = (device_data[16] << 8) | device_data[17];
	RPMB_LOG_DEBUG("UFS spec version 0x%x\n", wspecversion);

	ret = ufs_read_desc(rpmb.fd_ufs_bsg, geo_data,
			    QUERY_DESC_SIZE_GEOMETRY,
			    QUERY_DESC_IDN_GEOMETRY, 0);
	if (ret) {
		RPMB_LOG_ERROR("Error requesting ufs geometry info via query ioctl (return value: %d, error no: %d)\n",
				ret, errno);
		goto out;
	}

	/*
	 * According to JEDEC UFS spec, bRPMB_ReadWriteSize in Geometry Descriptor
	 * is the number of RPMB frames allowed in a single SECURITY_PROTOCOL_IN
	 * or SECURITY_PROTOCOL_OUT i.e. in a single command UPIU
	 */
	rpmb.info.rel_wr_count = geo_data[23];
	if (!rpmb.info.rel_wr_count) {
		/* diting : évite une division par zéro plus loin */
		RPMB_LOG_WARN("bRPMB_ReadWriteSize = 0, 1 trame par commande\n");
		rpmb.info.rel_wr_count = 1;
	}
	RPMB_LOG_DEBUG("bRPMB_ReadWriteSize: %.2x\n", geo_data[23]);

	ret = ufs_read_desc(rpmb.fd_ufs_bsg, unit_data,
			    QUERY_DESC_SIZE_UNIT,
			    QUERY_DESC_IDN_UNIT, UPIU_RPMB_LUN);
	if (ret) {
		RPMB_LOG_ERROR("Error requesting ufs rpmb unit description via query ioctl (return value: %d, error no: %d)\n",
				ret, errno);
		goto out;
	}

	if (wspecversion < 0x300) {
		/*
		 * calculate the size of the rpmb parition in sectors
		 * using only lower 32 bits for now
		 */
		rpmb_num_blocks = (unit_data[15] << 24) |
				  (unit_data[16] << 16) |
				  (unit_data[17] << 8) | unit_data[18];
		RPMB_LOG_DEBUG("rpmb num blocks: %x", rpmb_num_blocks);
		/*
		 * According to JEDE UFS spec, qLogicalBlockCount in RPMB Unit
		 * Descriptor is a multiple of 256. But TZ expects the number
		 * of sectors reported with sector size in 512 bytes hence
		 * report accordingly.
		 */
		rpmb.info.size = rpmb_num_blocks / 2;
	} else {
		/*
		 * calculate the size of the rpmb parition region 0 in sectors
		 * as we are using region 0 by default
		 */
		rpmb.info.size = unit_data[19] * 256;
		RPMB_LOG_DEBUG("rpmb region 0 num blocks: %x", rpmb.info.size);
	}

out:
	ufs_bsg_dev_close();
	return ret;
}

static int scsi_bsg_ioctl(int fd, __u8 *cdb, __u8 cdb_len, void *buf,
			  __u32 buf_len, enum bsg_ioctl_dir dir)
{
	int ret;
	struct sg_io_v4 sg_io = {0};
	unsigned char sense_buf[SENSE_BUF_LEN] = {0};

	sg_io.guard = 'Q';
	sg_io.protocol = BSG_PROTOCOL_SCSI;
	sg_io.subprotocol = BSG_SUB_PROTOCOL_SCSI_CMD;
	sg_io.request_len = cdb_len;
	sg_io.request = (__u64)(uintptr_t)cdb;
	sg_io.response = (__u64)(uintptr_t)sense_buf;
	sg_io.max_response_len = SENSE_BUF_LEN;
	if (dir == BSG_IOCTL_DIR_FROM_DEV) {
		sg_io.din_xfer_len = (__u32)buf_len;
		sg_io.din_xferp = (__u64)(uintptr_t)buf;
	} else {
		sg_io.dout_xfer_len = (__u32)buf_len;
		sg_io.dout_xferp = (__u64)(uintptr_t)buf;
	}

	ret = ioctl(fd, SG_IO, &sg_io);
	if (ret)
		RPMB_LOG_ERROR("%s: Error from sg_io ioctl (return value: %d, error no: %d)\n",
		     __func__, ret, errno);

	if (sg_io.info) {
		RPMB_LOG_ERROR("SCSI error occurred!!\n");
		RPMB_LOG_ERROR("----------------------------------------------------\n");
		RPMB_LOG_ERROR("%s: Error from sg_io info (check sg info: device_status: 0x%x, transport_status: 0x%x, driver_status: 0x%x, Sense Key code: 0x%x)\n",
		     __func__, sg_io.device_status, sg_io.transport_status,
		     sg_io.driver_status, (sense_buf[2] & 0xF));
		RPMB_LOG_ERROR("----------------------------------------------------\n");
		ret = -EIO;
	}

	return ret;
}

static int rpmb_ufs_send_request_sense(void)
{
	unsigned char cdb[SCSI_REQ_SENSE_CDB_LEN] = {0};
	unsigned char sense_buf[SCSI_REQ_SENSE_BUF_LEN] = {0};
	enum bsg_ioctl_dir dir = BSG_IOCTL_DIR_FROM_DEV;
	int ret = 0;

	cdb[0] = SCSI_REQ_SENSE_ID;
	cdb[4] = SCSI_REQ_SENSE_BUF_LEN;

	ret = rpmb_bsg_dev_open();
	if (ret)
		return ret;

	ret = scsi_bsg_ioctl(rpmb.fd, cdb, SCSI_REQ_SENSE_CDB_LEN,
			     sense_buf, SCSI_REQ_SENSE_BUF_LEN, dir);
	if (ret)
		RPMB_LOG_ERROR("%s: Error from scsi_bsg_ioctl (return value: %d, error no: %d)\n", __func__, ret, errno);

	rpmb_bsg_dev_close();
	return ret;
}

/*
 * rpmb_ufs_probe - check whether a UFS BSG device is present
 *
 * Read-only check: scans /dev/bsg for a ufs-bsg entry without
 * modifying any global state.  get_ufs_bsg_dev() is called later
 * by rpmb_ufs_init() to populate ufs_bsg_dev[] for actual I/O.
 */
device_id_type rpmb_ufs_probe(void)
{
	const char * const names[] = { "ufs-bsg", "ufs-bsg0", NULL };
	const char * const dirs[] = { "/dev/bsg", "/dev", NULL };
	DIR *dir;
	struct dirent *ent;
	int i, j;

	for (i = 0; dirs[i]; i++) {
		dir = opendir(dirs[i]);
		if (!dir)
			continue;
		while ((ent = readdir(dir)) != NULL) {
			for (j = 0; names[j]; j++) {
				if (strcmp(ent->d_name, names[j]) == 0) {
					closedir(dir);
					return UFS_RPMB;
				}
			}
		}
		closedir(dir);
	}
	return NO_DEVICE;
}

int rpmb_ufs_init(void)
{
	int ret;

	rpmb.fd = -1;
	rpmb.fd_ufs_bsg = -1;

	ret = get_ufs_bsg_dev();
	if (ret)
		return ret;
	RPMB_LOG_DEBUG("Found the ufs bsg dev: %s\n", ufs_bsg_dev);

	ret = get_rpmb_bsg_dev();
	if (ret)
		return ret;
	RPMB_LOG_DEBUG("Found the rpmb bsg dev: %s\n", rpmb_bsg_dev);

	ret = get_ufs_rpmb_parameters();
	if (ret < 0) {
		RPMB_LOG_ERROR("Error reading UFS descriptors (error no: %d)\n", ret);
		return ret;
	}
	RPMB_LOG_DEBUG("RPMB Mult (512-byte sector) = %d, Rel_sec_cnt = %d\n",
	     rpmb.info.size, rpmb.info.rel_wr_count);

	rpmb.info.dev_type = UFS_RPMB;
	rpmb.init_done = 1;

	ret = rpmb_ufs_send_request_sense();
	if (ret < 0) {
		RPMB_LOG_ERROR("Request sense command failed (error no: %d)\n", ret);
		return ret;
	}

	rpmb_init_wakelock();
	return 0;
}

int rpmb_ufs_read(uint32_t *req_buf, uint32_t blk_cnt, uint32_t *resp_buf,
		uint32_t *resp_len)
{
	uint32_t num_bytes, temp_blk_cnt = blk_cnt, blk_cnt_rem = blk_cnt;
	int ret = 0, num_rpmb_trans, i;
	unsigned char scsi_sec_out_cmd_cdb[SCSI_SEC_CDB_LEN];
	unsigned char scsi_sec_in_cmd_cdb[SCSI_SEC_CDB_LEN];
	uint32_t *req_buf_cached = NULL, *req_buf_offset = NULL;

	num_rpmb_trans = blk_cnt / rpmb.info.rel_wr_count;
	if (blk_cnt % rpmb.info.rel_wr_count)
		num_rpmb_trans++;

	/*
	 * Cache the rpmb request buffer if there are multiple RPMB transfers,
	 * otherwise request buffer contents may get overwritten when we copy
	 * the response for the first RPMB transfer.
	 */
	if (num_rpmb_trans > 1) {
		req_buf_cached = malloc(num_rpmb_trans * RPMB_BLK_SIZE);
		if (!req_buf_cached)
			return -ENOMEM;

		memcpy(req_buf_cached, req_buf,
		       (num_rpmb_trans * RPMB_BLK_SIZE));
		req_buf_offset = req_buf_cached;
	} else {
		req_buf_offset = req_buf;
	}

	rpmb_wakelock();

	ret = rpmb_bsg_dev_open();
	if (ret)
		goto out_free;

	for (i = 0; i < num_rpmb_trans; i++) {
		if ((blk_cnt_rem > 0) && (blk_cnt_rem <= rpmb.info.rel_wr_count)) {
			temp_blk_cnt = blk_cnt_rem;
		} else if (blk_cnt_rem > rpmb.info.rel_wr_count) {
			temp_blk_cnt = rpmb.info.rel_wr_count;
		} else {
			/* should not end up here */
			RPMB_LOG_ERROR("Error: incorrect block count calculation in reading rpmb data from ufs\t");
			RPMB_LOG_ERROR("blk_cnt_rem = %u, temp_blk_cnt = %u, i = %d\n", blk_cnt_rem, temp_blk_cnt, i);
		}
		num_bytes = temp_blk_cnt * RPMB_BLK_SIZE;

		/* Send a SPO cmd for a read request */
		memset(&scsi_sec_out_cmd_cdb, 0, SCSI_SEC_CDB_LEN);
		scsi_sec_out_cmd_cdb[0] = SCSI_SEC_OUT_ID;
		scsi_sec_out_cmd_cdb[1] = SCSI_SEC_PROT;

		scsi_sec_out_cmd_cdb[2] = (unsigned char)((SCSI_SEC_UFS_PROT_ID >> 8) & 0xff);
		scsi_sec_out_cmd_cdb[3] = (unsigned char)(SCSI_SEC_UFS_PROT_ID & 0xff);

		scsi_sec_out_cmd_cdb[6] = (unsigned char)((RPMB_BLK_SIZE >> 24) & 0xff);
		scsi_sec_out_cmd_cdb[7] = (unsigned char)((RPMB_BLK_SIZE >> 16) & 0xff);
		scsi_sec_out_cmd_cdb[8] = (unsigned char)((RPMB_BLK_SIZE >> 8) & 0xff);
		scsi_sec_out_cmd_cdb[9] = (unsigned char)(RPMB_BLK_SIZE & 0xff);

		ret = scsi_bsg_ioctl(rpmb.fd, scsi_sec_out_cmd_cdb, SCSI_SEC_CDB_LEN,
				     req_buf_offset, RPMB_BLK_SIZE, BSG_IOCTL_DIR_TO_DEV);
		if (ret) {
			RPMB_LOG_ERROR("%s: Error sending SPO through scsi_bsg_ioctl (return value: %d, error no: %d, iter: %d)\n", __func__, ret, errno, i);
			goto out;
		}

		/* Send a SPI cmd to read RPMB data frames back */
		memset(&scsi_sec_in_cmd_cdb, 0, SCSI_SEC_CDB_LEN);
		scsi_sec_in_cmd_cdb[0] = SCSI_SEC_IN_ID;
		scsi_sec_in_cmd_cdb[1] = SCSI_SEC_PROT;

		scsi_sec_in_cmd_cdb[2] = (unsigned char)((SCSI_SEC_UFS_PROT_ID >> 8) & 0xff);
		scsi_sec_in_cmd_cdb[3] = (unsigned char)(SCSI_SEC_UFS_PROT_ID & 0xff);

		scsi_sec_in_cmd_cdb[6] = (unsigned char)((num_bytes >> 24) & 0xff);
		scsi_sec_in_cmd_cdb[7] = (unsigned char)((num_bytes >> 16) & 0xff);
		scsi_sec_in_cmd_cdb[8] = (unsigned char)((num_bytes >> 8) & 0xff);
		scsi_sec_in_cmd_cdb[9] = (unsigned char)(num_bytes & 0xff);

		ret = scsi_bsg_ioctl(rpmb.fd, scsi_sec_in_cmd_cdb, SCSI_SEC_CDB_LEN,
				     resp_buf, num_bytes, BSG_IOCTL_DIR_FROM_DEV);
		if (ret) {
			RPMB_LOG_ERROR("%s: Error sending SPI through scsi_bsg_ioctl (return value: %d, error no: %d, iter: %d)\n", __func__, ret, errno, i);
			goto out;
		}

		/* Select the next RPMB frame */
		req_buf_offset = (uint32_t *)((void *)((uint8_t*)req_buf_offset + RPMB_BLK_SIZE));
		resp_buf = (uint32_t *)((void *)((uint8_t*)resp_buf + (temp_blk_cnt * RPMB_BLK_SIZE)));
		blk_cnt_rem -= temp_blk_cnt;
	}

out:
	rpmb_bsg_dev_close();
out_free:
	if (num_rpmb_trans > 1)
		free(req_buf_cached);
	*resp_len = blk_cnt * RPMB_BLK_SIZE;
	rpmb_wakeunlock();
	return ret;
}

static int rpmb_ufs_write_with_timeout(uint32_t *req_buf, uint32_t blk_cnt, uint32_t *resp_buf,
		uint32_t *resp_len, uint32_t frames_per_rpmb_trans)
{
	int i, num_rpmb_trans = 0;
	uint32_t result_frame_bytes = RPMB_BLK_SIZE;
	uint32_t req_frame_bytes = RPMB_BLK_SIZE * frames_per_rpmb_trans;
	int ret = 0;
	unsigned char scsi_sec_out_cmd_cdb[SCSI_SEC_CDB_LEN];
	unsigned char scsi_sec_in_cmd_cdb[SCSI_SEC_CDB_LEN];

	RPMB_LOG_DEBUG("UFS RPMB write starting: blk_cnt=%d, frames_per_trans=%d", blk_cnt, frames_per_rpmb_trans);

	rpmb_wakelock();
	ret = rpmb_bsg_dev_open();
	if (ret) {
		RPMB_LOG_ERROR("Failed to open BSG device: %d", ret);
		goto out_unlock;
	}

	/*
	 * Secure world should never send more than the reliable write count
	 * number of frames for a single operation. If in the future, the
	 * secure world sends all the rpmb requests in one shot, then it
	 * may be need to be supported in the future.
	 */
	if (!frames_per_rpmb_trans ||
	    frames_per_rpmb_trans > rpmb.info.rel_wr_count) {
		RPMB_LOG_ERROR("Incorrect numner of rpmb write operations requested\n");
		rpmb_bsg_dev_close();
		ret = -1;
		goto out_unlock;
	}

	num_rpmb_trans = blk_cnt / frames_per_rpmb_trans;

	for (i = num_rpmb_trans; i > 0; i--) {
		/* Send a SPO cmd to write RPMB data frames */
		memset(&scsi_sec_out_cmd_cdb, 0, SCSI_SEC_CDB_LEN);
		scsi_sec_out_cmd_cdb[0] = SCSI_SEC_OUT_ID;
		scsi_sec_out_cmd_cdb[1] = SCSI_SEC_PROT;

		scsi_sec_out_cmd_cdb[2] = (unsigned char)((SCSI_SEC_UFS_PROT_ID >> 8) & 0xff);
		scsi_sec_out_cmd_cdb[3] = (unsigned char)(SCSI_SEC_UFS_PROT_ID & 0xff);

		scsi_sec_out_cmd_cdb[6] = (unsigned char)((req_frame_bytes >> 24) & 0xff);
		scsi_sec_out_cmd_cdb[7] = (unsigned char)((req_frame_bytes >> 16) & 0xff);
		scsi_sec_out_cmd_cdb[8] = (unsigned char)((req_frame_bytes >> 8) & 0xff);
		scsi_sec_out_cmd_cdb[9] = (unsigned char)(req_frame_bytes & 0xff);

		ret = scsi_bsg_ioctl(rpmb.fd, scsi_sec_out_cmd_cdb, SCSI_SEC_CDB_LEN,
				     req_buf, req_frame_bytes, BSG_IOCTL_DIR_TO_DEV);
		if (ret) {
			RPMB_LOG_ERROR("%s: Error sending SPO through scsi_bsg_ioctl (return value: %d, error no: %d, iter: %d)\n", __func__, ret, errno, i);
			goto out;
		}

		/* Send a SPO cmd for a read request */
		memset(&scsi_sec_out_cmd_cdb, 0, SCSI_SEC_CDB_LEN);
		scsi_sec_out_cmd_cdb[0] = SCSI_SEC_OUT_ID;
		scsi_sec_out_cmd_cdb[1] = SCSI_SEC_PROT;

		scsi_sec_out_cmd_cdb[2] = (unsigned char)((SCSI_SEC_UFS_PROT_ID >> 8) & 0xff);
		scsi_sec_out_cmd_cdb[3] = (unsigned char)(SCSI_SEC_UFS_PROT_ID & 0xff);

		scsi_sec_out_cmd_cdb[6] = (unsigned char)((result_frame_bytes >> 24) & 0xff);
		scsi_sec_out_cmd_cdb[7] = (unsigned char)((result_frame_bytes >> 16) & 0xff);
		scsi_sec_out_cmd_cdb[8] = (unsigned char)((result_frame_bytes >> 8) & 0xff);
		scsi_sec_out_cmd_cdb[9] = (unsigned char)(result_frame_bytes & 0xff);

		ret = scsi_bsg_ioctl(rpmb.fd, scsi_sec_out_cmd_cdb, SCSI_SEC_CDB_LEN,
				     (void *)&read_result_reg_frame, result_frame_bytes, BSG_IOCTL_DIR_TO_DEV);
		if (ret) {
			RPMB_LOG_ERROR("%s: Error sending SPO through scsi_bsg_ioctl (return value: %d, error no: %d, iter: %d)\n", __func__, ret, errno, i);
			goto out;
		}

		/* Send a SPI cmd to read RPMB data frames back */
		memset(&scsi_sec_in_cmd_cdb, 0, SCSI_SEC_CDB_LEN);
		scsi_sec_in_cmd_cdb[0] = SCSI_SEC_IN_ID;
		scsi_sec_in_cmd_cdb[1] = SCSI_SEC_PROT;

		scsi_sec_in_cmd_cdb[2] = (unsigned char)((SCSI_SEC_UFS_PROT_ID >> 8) & 0xff);
		scsi_sec_in_cmd_cdb[3] = (unsigned char)(SCSI_SEC_UFS_PROT_ID & 0xff);

		scsi_sec_in_cmd_cdb[6] = (unsigned char)((result_frame_bytes >> 24) & 0xff);
		scsi_sec_in_cmd_cdb[7] = (unsigned char)((result_frame_bytes >> 16) & 0xff);
		scsi_sec_in_cmd_cdb[8] = (unsigned char)((result_frame_bytes >> 8) & 0xff);
		scsi_sec_in_cmd_cdb[9] = (unsigned char)(result_frame_bytes & 0xff);

		ret = scsi_bsg_ioctl(rpmb.fd, scsi_sec_in_cmd_cdb, SCSI_SEC_CDB_LEN,
				     resp_buf, result_frame_bytes, BSG_IOCTL_DIR_FROM_DEV);
		if (ret) {
			RPMB_LOG_ERROR("%s: Error sending SPO through scsi_bsg_ioctl (return value: %d, error no: %d, iter: %d)\n", __func__, ret, errno, i);
			goto out;
		}

		/* Select the next RPMB frame */
		req_buf = (uint32_t *)((void *)((uint8_t*)req_buf + (frames_per_rpmb_trans * RPMB_BLK_SIZE)));
	}

out:
	rpmb_bsg_dev_close();
	*resp_len = RPMB_MIN_BLK_CNT * RPMB_BLK_SIZE;
out_unlock:
	rpmb_wakeunlock();
	return ret;
}

int rpmb_ufs_write(uint32_t *req_buf, uint32_t blk_cnt, uint32_t *resp_buf,
		uint32_t *resp_len, uint32_t frames_per_rpmb_trans)
{
	int ret;

	ret = rpmb_ufs_write_with_timeout(req_buf, blk_cnt, resp_buf,
					  resp_len, frames_per_rpmb_trans);
	if (ret)
		RPMB_LOG_ERROR("UFS RPMB write failed: %d\n", ret);
	return ret;
}

void rpmb_ufs_exit(void)
{
	ufs_bsg_dev_close();
	rpmb_bsg_dev_close();
}
