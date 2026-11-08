/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DITING_FICHIERS_H
#define DITING_FICHIERS_H
#include <stddef.h>
#include <stdint.h>

#define FICHIERS_SERVICE_ID	0xa
#define FICHIERS_TAMPON		(20 * 1024)	/* FILE_SERVICE_BUF_LEN de minkipc */

#define FICHIERS_GP_SERVICE_ID	0x7000
#define FICHIERS_GP_TAMPON	(504 * 1024)	/* GPFILE_SERVICE_BUF_LEN */

/* sert une demande du listener 0x7000 */
int servir_fichiers_gp(uint8_t *va, size_t taille);

/* sert une demande du listener 0xa ; réponse écrite dans le même tampon */
int servir_fichiers(uint8_t *va, size_t taille);
#endif
