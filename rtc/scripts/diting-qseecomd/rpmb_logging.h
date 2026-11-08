// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Journal du service RPMB de minkipc, renvoyé sur stderr (journald).
 * Remplace le rpmb_logging.h d'origine (syslog). INFO et DEBUG seulement
 * avec -v.
 */
#ifndef __RPMB_LOGGING_H__
#define __RPMB_LOGGING_H__

#include <stdio.h>

extern int qseecomd_verbeux;

#define RPMB_LOG_ERROR(fmt, ...) fprintf(stderr, "<3>rpmb : " fmt, ##__VA_ARGS__)
#define RPMB_LOG_WARN(fmt, ...)  fprintf(stderr, "<4>rpmb : " fmt, ##__VA_ARGS__)
#define RPMB_LOG_INFO(fmt, ...) \
	do { if (qseecomd_verbeux) fprintf(stderr, "<6>rpmb : " fmt, ##__VA_ARGS__); } while (0)
#define RPMB_LOG_DEBUG(fmt, ...) \
	do { if (qseecomd_verbeux > 1) fprintf(stderr, "<7>rpmb : " fmt, ##__VA_ARGS__); } while (0)

#endif
