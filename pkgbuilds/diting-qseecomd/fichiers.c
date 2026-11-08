// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Services fichiers de diting-qseecomd, en LECTURE SEULE :
 *   0xa     fichiers « POSIX » (open/read/lseek/fstat/close…) ;
 *   0x7000  fichiers GP (lecture d'un morceau d'un fichier en une demande).
 *
 * Les applis QSEE (gfdit : calibration d'usine du capteur d'empreintes)
 * lisent leurs fichiers du monde normal par ce listener. Le format des
 * messages (numéros de commande, structures « packed ») est repris de
 * qualcomm/minkipc, listeners/libfsservice/fs/fs_msg.h et gpfs/gpfs_msg.h
 * (BSD-3-Clause-Clear,
 * voir LICENSE.minkipc) ; le code ci-dessous est une réécriture.
 *
 * Garde-fous :
 *   - seuls les chemins sous /mnt/vendor/persist/ sont servis, sans « .. »,
 *     sans suivre de lien symbolique en fin de chemin ;
 *   - toute modification (écriture, création, troncature, suppression,
 *     renommage, mkdir, chmod…) est refusée avec EROFS ; une ouverture en
 *     lecture-écriture sans O_TRUNC est servie en lecture seule ;
 *   - seuls les descripteurs ouverts par ce service sont acceptés ensuite ;
 *   - rien n'est servi si la partition persist n'est pas montée (EAGAIN).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#include "fichiers.h"

#define LOG(niv, fmt, ...) fprintf(stderr, "<" #niv ">" fmt "\n", ##__VA_ARGS__)

extern int qseecomd_verbeux;

#ifndef RACINE
#define RACINE		"/mnt/vendor/persist"
#endif
#define NOM_MAX		256	/* TZ_CM_MAX_NAME_LEN */
#define DONNEES_MAX	20000	/* TZ_CM_MAX_DATA_LEN */
#define MAX_FD		32
#define MAX_DIR		8

enum {
	FS_OPEN = 0x202, FS_OPENAT, FS_UNLINKAT, FS_FCNTL, FS_CREAT, FS_READ,
	FS_WRITE, FS_CLOSE, FS_LSEEK, FS_LINK, FS_UNLINK, FS_RMDIR, FS_FSTAT,
	FS_LSTAT, FS_MKDIR, FS_TESTDIR, FS_TELLDIR, FS_REMOVE, FS_CHOWN_CHMOD,
	FS_UNUSED, FS_SYNC, FS_RENAME, FS_PAR_FR_SIZE, FS_DIR_OPEN, FS_DIR_READ,
	FS_DIR_CLOSE, FS_GET_ERRNO, FS_END,
};

struct tz_stat {
	uint64_t st_dev;
	uint8_t pad0[4];
	uint32_t st_ino32;
	uint32_t st_mode;
	uint32_t st_nlink;
	uint32_t st_uid;
	uint32_t st_gid;
	uint64_t st_rdev;
	uint8_t pad3[4];
	int64_t st_size;
	uint32_t st_blksize;
	uint64_t st_blocks;
	uint32_t st_atim, st_atim_nsec;
	uint32_t st_mtim, st_mtim_nsec;
	uint32_t st_ctim, st_ctim_nsec;
	uint64_t st_ino;
} __attribute__((packed));

struct tz_dirent {
	uint64_t d_ino;
	int64_t d_off;
	uint16_t d_reclen;
	uint8_t d_type;
	char d_name[NOM_MAX];
} __attribute__((packed));

/* réponse générique : { cmd_id, ret } */
struct rep_simple { uint32_t cmd; int32_t ret; } __attribute__((packed));

struct req_open { uint32_t cmd; char chemin[NOM_MAX]; int32_t flags; } __attribute__((packed));
struct req_openat { uint32_t cmd; int32_t dirfd; char chemin[NOM_MAX]; int32_t flags; } __attribute__((packed));
struct req_fd { uint32_t cmd; int32_t fd; } __attribute__((packed));
struct req_fcntl { uint32_t cmd; int32_t fd; int32_t op; } __attribute__((packed));
struct req_read { uint32_t cmd; int32_t fd; uint32_t nb; } __attribute__((packed));
struct rep_read { uint32_t cmd; uint8_t buf[DONNEES_MAX]; int32_t ret; } __attribute__((packed));
struct req_lseek { uint32_t cmd; int32_t fd; int32_t off; int32_t whence; } __attribute__((packed));
struct rep_stat { uint32_t cmd; struct tz_stat st; int32_t ret; } __attribute__((packed));
struct req_chemin { uint32_t cmd; char chemin[NOM_MAX]; } __attribute__((packed));
struct rep_libre { uint32_t cmd; uint64_t taille; int32_t ret; } __attribute__((packed));
struct rep_dir_open { uint32_t cmd; uint64_t dir; int32_t ret; } __attribute__((packed));
struct req_dir { uint32_t cmd; uint64_t dir; } __attribute__((packed));
struct rep_dir_read { uint32_t cmd; struct tz_dirent ent; int32_t ret; } __attribute__((packed));

_Static_assert(sizeof(struct tz_stat) == 96, "tz_stat");
_Static_assert(sizeof(struct rep_read) == 20008, "rep_read");

static int fs_errno;
static int fds[MAX_FD];
static int nb_fds;
static DIR *dirs[MAX_DIR];

static int persist_montee(void)
{
	struct stat a, b;

	return !stat(RACINE, &a) && !stat(RACINE "/..", &b) && a.st_dev != b.st_dev;
}

/* chemin terminé par NUL dans NOM_MAX, sous RACINE, sans composant « .. » */
static const char *chemin_ok(const char *c)
{
	size_t n = strnlen(c, NOM_MAX);

	if (n == NOM_MAX || strncmp(c, RACINE, strlen(RACINE)) ||
	    (c[strlen(RACINE)] != '/' && c[strlen(RACINE)] != '\0'))
		return NULL;
	for (const char *p = c; (p = strstr(p, "..")); p += 2)
		if ((p == c || p[-1] == '/') && (p[2] == '/' || p[2] == '\0'))
			return NULL;
	return c;
}

static int fd_ok(int fd)
{
	for (int i = 0; i < nb_fds; i++)
		if (fds[i] == fd)
			return 1;
	return 0;
}

static void fd_retirer(int fd)
{
	for (int i = 0; i < nb_fds; i++)
		if (fds[i] == fd) {
			fds[i] = fds[--nb_fds];
			return;
		}
}

static int echec(int e)
{
	fs_errno = e;
	return -1;
}

static int ouvrir(const char *c, int flags)
{
	int acc = flags & O_ACCMODE, fd;

	if (!chemin_ok(c)) {
		LOG(4, "fichiers : chemin refusé « %.*s »", NOM_MAX, c);
		return echec(EACCES);
	}
	if (acc == O_WRONLY || (flags & (O_TRUNC | O_APPEND))) {
		LOG(4, "fichiers : ouverture en écriture REFUSÉE %s (flags 0x%x)", c, flags);
		return echec(EROFS);
	}
	if (nb_fds >= MAX_FD)
		return echec(EMFILE);
	fd = open(c, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		LOG(6, "fichiers : ouvrir %s (flags 0x%x) : %s", c, flags, strerror(errno));
		return echec(errno);
	}
	fds[nb_fds++] = fd;
	LOG(6, "fichiers : ouvert %s (flags 0x%x%s) -> %d", c, flags,
	    acc == O_RDWR ? ", servi en lecture seule" : "", fd);
	return fd;
}

static void remplir_stat(struct tz_stat *t, const struct stat *s)
{
	memset(t, 0, sizeof(*t));
	t->st_dev = s->st_dev;
	t->st_mode = s->st_mode;
	t->st_nlink = s->st_nlink;
	t->st_uid = s->st_uid;
	t->st_gid = s->st_gid;
	t->st_rdev = s->st_rdev;
	t->st_size = s->st_size;
	t->st_blksize = s->st_blksize;
	t->st_blocks = s->st_blocks;
	t->st_atim = s->st_atim.tv_sec;
	t->st_atim_nsec = s->st_atim.tv_nsec;
	t->st_mtim = s->st_mtim.tv_sec;
	t->st_mtim_nsec = s->st_mtim.tv_nsec;
	t->st_ctim = s->st_ctim.tv_sec;
	t->st_ctim_nsec = s->st_ctim.tv_nsec;
	t->st_ino = s->st_ino;
}

int servir_fichiers(uint8_t *va, size_t taille)
{
	uint32_t cmd = *(uint32_t *)va;
	struct rep_simple *rs = (void *)va;

	if (taille < sizeof(struct rep_read))
		return -1;

	if (cmd != FS_GET_ERRNO && cmd != FS_END && !persist_montee()) {
		LOG(4, "fichiers : %s n'est pas montée, commande 0x%x refusée", RACINE, cmd);
		fs_errno = EAGAIN;
		switch (cmd) {
		case FS_READ:
			((struct rep_read *)va)->ret = -1;
			break;
		case FS_FSTAT:
		case FS_LSTAT:
			((struct rep_stat *)va)->ret = -1;
			break;
		case FS_PAR_FR_SIZE:
			((struct rep_libre *)va)->ret = -1;
			break;
		case FS_DIR_OPEN:
			((struct rep_dir_open *)va)->ret = -1;
			break;
		case FS_DIR_READ:
			((struct rep_dir_read *)va)->ret = -1;
			break;
		default:
			rs->ret = -1;
		}
		return 0;
	}

	switch (cmd) {
	case FS_OPEN: {
		struct req_open q = *(struct req_open *)va;

		rs->ret = ouvrir(q.chemin, q.flags);
		break;
	}
	case FS_OPENAT: {
		struct req_openat q = *(struct req_openat *)va;

		/* chemins absolus seulement : dirfd ignoré */
		rs->ret = ouvrir(q.chemin, q.flags);
		break;
	}
	case FS_FCNTL: {
		struct req_fcntl q = *(struct req_fcntl *)va;

		rs->ret = fd_ok(q.fd) && q.op == F_GETFL ? O_RDONLY : echec(EINVAL);
		break;
	}
	case FS_READ: {
		struct req_read q = *(struct req_read *)va;
		struct rep_read *r = (void *)va;
		ssize_t n;

		if (!fd_ok(q.fd)) {
			r->ret = echec(EBADF);
			break;
		}
		n = read(q.fd, r->buf, q.nb < DONNEES_MAX ? q.nb : DONNEES_MAX);
		r->ret = n < 0 ? echec(errno) : (int32_t)n;
		if (qseecomd_verbeux > 1)
			LOG(6, "fichiers : lire %d : %u demandés, %d lus", q.fd, q.nb, r->ret);
		break;
	}
	case FS_CLOSE: {
		struct req_fd q = *(struct req_fd *)va;

		if (!fd_ok(q.fd)) {
			rs->ret = echec(EBADF);
			break;
		}
		fd_retirer(q.fd);
		rs->ret = close(q.fd) ? echec(errno) : 0;
		LOG(6, "fichiers : fermé %d", q.fd);
		break;
	}
	case FS_LSEEK: {
		struct req_lseek q = *(struct req_lseek *)va;
		off_t o;

		if (!fd_ok(q.fd)) {
			rs->ret = echec(EBADF);
			break;
		}
		o = lseek(q.fd, q.off, q.whence);
		rs->ret = o < 0 ? echec(errno) : (o > INT32_MAX ? echec(EOVERFLOW) : (int32_t)o);
		break;
	}
	case FS_FSTAT: {
		struct req_fd q = *(struct req_fd *)va;
		struct rep_stat *r = (void *)va;
		struct stat s;

		if (!fd_ok(q.fd)) {
			r->ret = echec(EBADF);
			break;
		}
		if (fstat(q.fd, &s)) {
			r->ret = echec(errno);
			break;
		}
		remplir_stat(&r->st, &s);
		r->ret = 0;
		break;
	}
	case FS_LSTAT: {
		struct req_chemin q = *(struct req_chemin *)va;
		struct rep_stat *r = (void *)va;
		struct stat s;

		if (!chemin_ok(q.chemin)) {
			r->ret = echec(EACCES);
			break;
		}
		if (lstat(q.chemin, &s)) {
			LOG(6, "fichiers : lstat %s : %s", q.chemin, strerror(errno));
			r->ret = echec(errno);
			break;
		}
		remplir_stat(&r->st, &s);
		r->ret = 0;
		break;
	}
	case FS_TESTDIR: {
		struct req_chemin q = *(struct req_chemin *)va;
		struct stat s;

		if (!chemin_ok(q.chemin))
			rs->ret = echec(ENOENT);
		else if (stat(q.chemin, &s))
			rs->ret = echec(errno);
		else
			rs->ret = S_ISDIR(s.st_mode) ? 0 : echec(ENOTDIR);
		LOG(6, "fichiers : testdir %s -> %d", q.chemin, rs->ret);
		break;
	}
	case FS_SYNC: {
		struct req_fd q = *(struct req_fd *)va;

		rs->ret = fd_ok(q.fd) ? 0 : echec(EBADF);
		break;
	}
	case FS_PAR_FR_SIZE: {
		struct rep_libre *r = (void *)va;
		struct statfs f;

		r->taille = 0;
		r->ret = statfs(RACINE, &f) ? echec(errno) : 0;
		/* lecture seule : aucune place libre à proposer */
		break;
	}
	case FS_DIR_OPEN: {
		struct req_chemin q = *(struct req_chemin *)va;
		struct rep_dir_open *r = (void *)va;
		int i;

		r->dir = 0;
		for (i = 0; i < MAX_DIR && dirs[i]; i++)
			;
		if (!chemin_ok(q.chemin) || i == MAX_DIR) {
			r->ret = -3;	/* E_FS_DIR_NOT_EXIST */
			fs_errno = ENOENT;
			break;
		}
		dirs[i] = opendir(q.chemin);
		if (!dirs[i]) {
			r->ret = -3;
			fs_errno = errno;
			break;
		}
		r->dir = i + 1;	/* jeton, jamais un pointeur */
		r->ret = 0;
		LOG(6, "fichiers : dossier %s ouvert", q.chemin);
		break;
	}
	case FS_DIR_READ: {
		struct req_dir q = *(struct req_dir *)va;
		struct rep_dir_read *r = (void *)va;
		struct dirent *e;

		memset(&r->ent, 0, sizeof(r->ent));
		if (!q.dir || q.dir > MAX_DIR || !dirs[q.dir - 1]) {
			r->ret = -2;	/* E_FS_INVALID_ARG */
			fs_errno = EINVAL;
			break;
		}
		e = readdir(dirs[q.dir - 1]);
		if (e) {
			r->ent.d_ino = e->d_ino;
			r->ent.d_off = e->d_off;
			r->ent.d_reclen = e->d_reclen;
			r->ent.d_type = e->d_type;
			memcpy(r->ent.d_name, e->d_name, strnlen(e->d_name, NOM_MAX - 1));
		}
		r->ret = 0;
		break;
	}
	case FS_DIR_CLOSE: {
		struct req_dir q = *(struct req_dir *)va;

		if (!q.dir || q.dir > MAX_DIR || !dirs[q.dir - 1]) {
			rs->ret = -2;
			fs_errno = EINVAL;
			break;
		}
		closedir(dirs[q.dir - 1]);
		dirs[q.dir - 1] = NULL;
		rs->ret = 0;
		break;
	}
	case FS_TELLDIR:
		rs->ret = echec(ENOSYS);
		break;
	case FS_GET_ERRNO:
		rs->ret = fs_errno;
		break;
	case FS_END:
		rs->ret = 0;
		break;
	case FS_UNLINKAT:
	case FS_CREAT:
	case FS_WRITE:
	case FS_LINK:
	case FS_UNLINK:
	case FS_RMDIR:
	case FS_MKDIR:
	case FS_REMOVE:
	case FS_CHOWN_CHMOD:
	case FS_RENAME:
		LOG(4, "fichiers : commande de modification 0x%x REFUSÉE (lecture seule)", cmd);
		if (cmd != FS_WRITE)
			LOG(4, "fichiers :   chemin « %.*s »", NOM_MAX - 8, (char *)va + 4);
		rs->ret = echec(EROFS);
		break;
	default:
		LOG(4, "fichiers : commande inconnue 0x%x", cmd);
		rs->ret = -1;
		break;
	}
	rs->cmd = cmd;
	return 0;
}

/* ---- fichiers GP (0x7000), d'après minkipc gpfs_msg.h ------------------- */

/*
 * minkipc ne connaît que les commandes 4 à 12 (fichiers relatifs à un dossier
 * « data » ou « persist »). gfdit envoie la commande 0 avec un chemin absolu :
 * c'est l'ancienne lecture « FILE_READ » de qseecomd (1, 2, 3 : écriture,
 * suppression, renommage), au même format que les lectures 4 et 8.
 */
enum {
	GP_READ = 0, GP_WRITE, GP_REMOVE, GP_RENAME,
	GP_DATA_READ, GP_DATA_WRITE, GP_DATA_REMOVE, GP_DATA_RENAME,
	GP_PERSIST_READ, GP_PERSIST_WRITE, GP_PERSIST_REMOVE, GP_PERSIST_RENAME,
	GP_VERSION,
};

#define GP_DONNEES_MAX	(500 * 1024)	/* TZ_GPFS_FILE_SIZE */

struct gp_req_read { uint32_t cmd; char chemin[NOM_MAX]; int32_t off; uint32_t nb; } __attribute__((packed));
struct gp_rep_read { uint32_t cmd; int32_t err; uint32_t lus; uint8_t buf[GP_DONNEES_MAX]; } __attribute__((packed));
struct gp_rep_err { uint32_t cmd; int32_t err; } __attribute__((packed));
struct gp_rep_version { uint32_t cmd; uint32_t version; int32_t err; } __attribute__((packed));

static int gp_lire(const char *c, int32_t off, uint32_t nb, struct gp_rep_read *r)
{
	size_t total = 0;
	int fd;

	if (!chemin_ok(c)) {
		LOG(4, "fichiers GP : chemin refusé « %.*s »", NOM_MAX, c);
		return EACCES;
	}
	if (off < 0)
		return EINVAL;
	fd = open(c, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		LOG(6, "fichiers GP : lire %s : %s", c, strerror(errno));
		return errno;
	}
	if (nb > GP_DONNEES_MAX)
		nb = GP_DONNEES_MAX;
	while (total < nb) {
		ssize_t n = pread(fd, r->buf + total, nb - total, off + total);

		if (n < 0) {
			int e = errno;

			close(fd);
			return e;
		}
		if (!n)
			break;
		total += n;
	}
	close(fd);
	r->lus = total;
	LOG(6, "fichiers GP : lu %s, %zu octets à l'octet %d (%u demandés)", c, total, off, nb);
	return 0;
}

int servir_fichiers_gp(uint8_t *va, size_t taille)
{
	uint32_t cmd = *(uint32_t *)va;
	struct gp_rep_err *re = (void *)va;

	if (taille < sizeof(struct gp_rep_read))
		return -1;

	if (cmd == GP_VERSION) {
		struct gp_rep_version *r = (void *)va;

		r->version = 2;	/* GP_FS_VERSION */
		r->err = 0;
		LOG(6, "fichiers GP : version demandée");
		return 0;
	}
	if (!persist_montee()) {
		LOG(4, "fichiers GP : %s n'est pas montée, commande %u refusée", RACINE, cmd);
		re->err = EAGAIN;
		return 0;
	}

	switch (cmd) {
	case GP_READ:
	case GP_PERSIST_READ: {
		struct gp_req_read q = *(struct gp_req_read *)va;
		struct gp_rep_read *r = (void *)va;
		char chemin[NOM_MAX + sizeof(RACINE) + 1];

		q.chemin[NOM_MAX - 1] = '\0';
		/* lecture « persist » : chemin relatif à la partition persist */
		if (cmd == GP_PERSIST_READ && q.chemin[0] != '/')
			snprintf(chemin, sizeof(chemin), RACINE "/%s", q.chemin);
		else
			snprintf(chemin, sizeof(chemin), "%s", q.chemin);
		r->lus = 0;
		r->err = gp_lire(chemin, q.off, q.nb, r);
		if (r->err)
			r->lus = 0;
		break;
	}
	case GP_DATA_READ: {
		struct gp_req_read q = *(struct gp_req_read *)va;

		LOG(4, "fichiers GP : lecture « data » %.*s non servie", NOM_MAX, q.chemin);
		re->err = ENOENT;
		break;
	}
	case GP_WRITE:
	case GP_REMOVE:
	case GP_RENAME:
	case GP_DATA_WRITE:
	case GP_DATA_REMOVE:
	case GP_DATA_RENAME:
	case GP_PERSIST_WRITE:
	case GP_PERSIST_REMOVE:
	case GP_PERSIST_RENAME:
		LOG(4, "fichiers GP : commande de modification %u REFUSÉE (lecture seule) « %.*s »",
		    cmd, NOM_MAX - 8, (char *)va + 4);
		re->err = EROFS;
		break;
	default:
		LOG(4, "fichiers GP : commande inconnue %u", cmd);
		re->err = -1;	/* GPFS_ERROR_NO_CMD */
		break;
	}
	return 0;
}
