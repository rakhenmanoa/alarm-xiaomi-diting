// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * diting-qseecomd : services (« listeners ») que les applis TrustZone QSEE
 * demandent au monde normal, à la place du qseecomd d'Android.
 *
 * Une appli QSEE qui doit lire ou écrire son stockage sécurisé s'interrompt et
 * demande un listener par son numéro ; elle attend la réponse. Sans réponse,
 * l'opération échoue ou reste en mémoire : c'est ce qui empêchait uefisecapp
 * d'enregistrer les variables UEFI (RTCInfo) dans le RPMB de l'UFS.
 *
 * Passe par le pilote TEE « qseecom » (patch 0918, impl_id 5) :
 *   - enregistrement : session sur /dev/teeprivN, paramètre 0 = numéro du
 *     listener (VALUE_INPUT), paramètre 1 = tampon partagé (MEMREF_INOUT) ;
 *   - service : TEE_IOC_SUPPL_RECV (func = numéro du listener, param 0 =
 *     génération de la requête, param 1 = tampon), puis TEE_IOC_SUPPL_SEND
 *     (ret 0 = succès, param 0 VALUE_OUTPUT = génération renvoyée).
 *
 * Listeners servis :
 *   0x2000  RPMB (protocole et accès UFS repris de qualcomm/minkipc,
 *           listeners/librpmbservice : rpmb.c, rpmb_ufs.c)
 *
 * Le contenu du RPMB est chiffré et authentifié par la TrustZone : ce
 * programme ne fait que transporter des trames qu'il ne peut ni lire ni
 * forger. Une trame mal transmise est refusée par la puce UFS (MAC, compteur).
 *
 * Options :
 *   -w        autoriser les écritures RPMB (sinon : lecture seule, les
 *             demandes d'écriture sont journalisées et refusées)
 *   -v        journal détaillé (-vv : encore plus)
 *   -e ID     enregistrer aussi le listener ID (décimal ou 0x…), seulement
 *             pour journaliser ses demandes, refusées ; répétable
 *   -n        ne pas enregistrer le listener RPMB (essais avec -e)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <linux/tee.h>

#include "rpmb.h"
#include "rpmb_service.h"

#ifndef TEE_IMPL_ID_QSEECOM
#define TEE_IMPL_ID_QSEECOM 5
#endif

#define RPMB_TAMPON	0x6400	/* taille enregistrée par le qseecomd d'Android */
#define AUTRE_TAMPON	0x1000
#define MAX_LISTENERS	8

int qseecomd_verbeux;
static int ecritures, sans_rpmb;
static int fd = -1;

struct listener {
	uint32_t id;
	int shm_id;
	size_t taille;
	uint8_t *va;
	uint32_t session;
	int (*servir)(struct listener *l);
};

static struct listener listeners[MAX_LISTENERS];
static int nb_listeners;

#define LOG(niv, fmt, ...) fprintf(stderr, "<" #niv ">" fmt "\n", ##__VA_ARGS__)

static void vidage(const char *titre, const uint8_t *p, size_t n)
{
	fprintf(stderr, "<6>%s :", titre);
	for (size_t i = 0; i < n; i++)
		fprintf(stderr, "%s%02x", i % 16 ? " " : "\n<6>  ", p[i]);
	fputc('\n', stderr);
}

/* ---- RPMB (0x2000), d'après minkipc rpmb_service.c ---------------------- */

static int rpmb_init_cmd(struct listener *l)
{
	tz_sd_device_init_req_t *req = (void *)l->va;
	uint32_t cmd = req->cmd_id, version = req->version;
	rpmb_init_info_t info = { 0 };
	int st = rpmb_init(&info);

	LOG(6, "RPMB init (version %u) : statut %d, %u secteurs de 512 o, %u trames par commande",
	    version, st, info.size, info.rel_wr_count);
	memset(l->va, 0, sizeof(tz_sd_device_init_res_v02_t));
	if (version >= RPMB_LSTNR_VERSION_2) {
		tz_sd_device_init_res_v02_t *r = (void *)l->va;

		r->cmd_id = cmd;
		r->version = RPMB_LSTNR_VERSION_2;
		r->status = st;
		r->num_sectors = info.size;
		r->rel_wr_count = info.rel_wr_count;
		r->dev_type = info.dev_type;
	} else {
		tz_sd_device_init_res_t *r = (void *)l->va;

		r->cmd_id = cmd;
		r->version = RPMB_LSTNR_VERSION_1;
		r->status = version == RPMB_LSTNR_VERSION_1 ? st : -1;
		r->num_sectors = version == RPMB_LSTNR_VERSION_1 ? info.size : 0;
		r->rel_wr_count = version == RPMB_LSTNR_VERSION_1 ? info.rel_wr_count : 0;
	}
	return 0;
}

/*
 * Lecture et écriture : les trames de la requête sont recopiées à part avant
 * tout accès, car la réponse est écrite dans le même tampon, juste après son
 * en-tête, et pourrait recouvrir des trames pas encore envoyées.
 */
static int rpmb_rw_cmd(struct listener *l, int ecrire)
{
	tz_rpmb_rw_req_t req = *(tz_rpmb_rw_req_t *)l->va;
	tz_rpmb_rw_res_t *res = (void *)l->va;
	size_t octets = (size_t)req.num_sectors * RPMB_BLK_SIZE;
	size_t rep_max = l->taille - sizeof(*res);
	uint32_t rep_len = 0;
	uint32_t *trames;
	int st;

	if (!req.num_sectors || req.req_buff_offset > l->taille ||
	    octets > l->taille - req.req_buff_offset) {
		LOG(3, "RPMB %s : requête hors du tampon (%u trames à l'octet %u)",
		    ecrire ? "écriture" : "lecture", req.num_sectors, req.req_buff_offset);
		return -1;
	}
	/* lecture : la réponse fait autant de trames que la requête en demande */
	if (!ecrire && octets > rep_max) {
		LOG(3, "RPMB lecture : %u trames, tampon trop petit", req.num_sectors);
		return -1;
	}
	trames = malloc(octets);
	if (!trames)
		return -1;
	memcpy(trames, l->va + req.req_buff_offset, octets);

	if (ecrire && !ecritures) {
		LOG(4, "RPMB écriture de %u trames REFUSÉE (lecture seule, relancer avec -w)",
		    req.num_sectors);
		st = -1;
	} else if (ecrire) {
		LOG(6, "RPMB écriture : %u trames, %u par opération", req.num_sectors,
		    req.rel_wr_count);
		st = rpmb_write(trames, req.num_sectors,
				(uint32_t *)(l->va + sizeof(*res)), &rep_len,
				req.rel_wr_count);
	} else {
		if (qseecomd_verbeux)
			LOG(6, "RPMB lecture : %u trames", req.num_sectors);
		st = rpmb_read(trames, req.num_sectors,
			       (uint32_t *)(l->va + sizeof(*res)), &rep_len);
	}
	free(trames);

	res->cmd_id = req.cmd_id;
	res->status = st;
	res->res_buff_len = rep_len;
	res->res_buff_offset = sizeof(*res);
	res->version = req.version;
	if (st)
		LOG(3, "RPMB %s : échec %d", ecrire ? "écriture" : "lecture", st);
	return 0;
}

static int rpmb_partition_cmd(struct listener *l)
{
	tz_sd_rpmb_partition_req_t req = *(tz_sd_rpmb_partition_req_t *)l->va;
	tz_sd_rpmb_partition_rsp_t *r = (void *)l->va;

	LOG(6, "RPMB table de partitions (version 0x%x) : non fournie", req.version);
	r->cmd_id = req.cmd_id;
	r->status = (uint32_t)-1;
	r->num_partitions = 0;
	r->rsp_buff_offset = sizeof(*r);
	return 0;
}

static int servir_rpmb(struct listener *l)
{
	uint32_t cmd = *(uint32_t *)l->va;

	switch (cmd) {
	case TZ_CM_CMD_RPMB_INIT:
		return rpmb_init_cmd(l);
	case TZ_CM_CMD_RPMB_READ:
		return rpmb_rw_cmd(l, 0);
	case TZ_CM_CMD_RPMB_WRITE:
		return rpmb_rw_cmd(l, 1);
	case TZ_CM_CMD_RPMB_PARTITION:
		return rpmb_partition_cmd(l);
	default:
		LOG(3, "RPMB : commande inconnue 0x%x", cmd);
		vidage("requête", l->va, 64);
		return -1;
	}
}

/* listener seulement observé : on journalise la demande et on la refuse */
static int servir_journal(struct listener *l)
{
	char t[64];

	snprintf(t, sizeof(t), "listener 0x%x : demande refusée", l->id);
	vidage(t, l->va, 64);
	return -1;
}

/* ---- pilote TEE ---------------------------------------------------------- */

/* le module qcom_qseecom_tee peut arriver après nous au démarrage : 30 s */
static int ouvrir_priv(void)
{
	char path[32];

	for (int essai = 0; essai < 300; essai++) {
	for (int i = 0; i < 8; i++) {
		struct tee_ioctl_version_data v;

		snprintf(path, sizeof(path), "/dev/teepriv%d", i);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (!ioctl(fd, TEE_IOC_VERSION, &v) && v.impl_id == TEE_IMPL_ID_QSEECOM) {
			LOG(6, "périphérique privilégié : %s", path);
			return 0;
		}
		close(fd);
		fd = -1;
	}
	usleep(100 * 1000);
	}
	LOG(3, "aucun /dev/teeprivN qseecom (module qcom_qseecom_tee chargé ? droits root ?)");
	return -1;
}

static int enregistrer(uint32_t id, size_t taille, int (*servir)(struct listener *))
{
	struct listener *l = &listeners[nb_listeners];
	struct tee_ioctl_shm_alloc_data d = { .size = taille };
	struct {
		struct tee_ioctl_open_session_arg arg;
		struct tee_ioctl_param p[2];
	} b;
	struct tee_ioctl_buf_data bd = { .buf_ptr = (uintptr_t)&b, .buf_len = sizeof(b) };
	int sfd;

	if (nb_listeners >= MAX_LISTENERS)
		return -1;
	sfd = ioctl(fd, TEE_IOC_SHM_ALLOC, &d);
	if (sfd < 0) {
		LOG(3, "listener 0x%x : mémoire partagée : %s", id, strerror(errno));
		return -1;
	}
	l->va = mmap(NULL, d.size, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
	close(sfd);	/* la mémoire reste tenue par le mmap et le contexte */
	if (l->va == MAP_FAILED) {
		LOG(3, "listener 0x%x : mmap : %s", id, strerror(errno));
		return -1;
	}
	memset(l->va, 0, d.size);
	l->id = id;
	l->shm_id = d.id;
	l->taille = taille;
	l->servir = servir;

	memset(&b, 0, sizeof(b));
	b.arg.num_params = 2;
	b.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	b.p[0].a = id;
	b.p[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	b.p[1].a = 0;
	b.p[1].b = taille;
	b.p[1].c = d.id;
	if (ioctl(fd, TEE_IOC_OPEN_SESSION, &bd) || b.arg.ret) {
		LOG(3, "listener 0x%x (%zu octets) : enregistrement refusé : %s (ret 0x%x) ; "
		    "voir dmesg | grep qseecom", id, taille, strerror(errno), b.arg.ret);
		return -1;
	}
	l->session = b.arg.session;
	nb_listeners++;
	LOG(6, "listener 0x%x enregistré (%zu octets)", id, taille);
	return 0;
}

static struct listener *trouver(uint32_t id, int shm_id)
{
	for (int i = 0; i < nb_listeners; i++)
		if (listeners[i].id == id && listeners[i].shm_id == shm_id)
			return &listeners[i];
	return NULL;
}

static void notifier_systemd(void)
{
	const char *s = getenv("NOTIFY_SOCKET");
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	int nfd;

	if (!s || !*s || strlen(s) >= sizeof(sa.sun_path))
		return;
	strcpy(sa.sun_path, s);
	if (sa.sun_path[0] == '@')
		sa.sun_path[0] = '\0';
	nfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (nfd < 0)
		return;
	sendto(nfd, "READY=1", 7, 0, (struct sockaddr *)&sa,
	       offsetof(struct sockaddr_un, sun_path) + strlen(s));
	close(nfd);
}

static volatile sig_atomic_t fin;

static void sur_signal(int sig)
{
	(void)sig;
	fin = 1;
}

static int boucle(void)
{
	while (!fin) {
		struct {
			struct tee_iocl_supp_recv_arg arg;
			struct tee_ioctl_param p[2];
		} r;
		struct {
			struct tee_iocl_supp_send_arg arg;
			struct tee_ioctl_param p[1];
		} s;
		struct tee_ioctl_buf_data rbd = { .buf_ptr = (uintptr_t)&r, .buf_len = sizeof(r) };
		struct tee_ioctl_buf_data sbd = { .buf_ptr = (uintptr_t)&s, .buf_len = sizeof(s) };
		struct listener *l;
		int ok;

		memset(&r, 0, sizeof(r));
		r.arg.num_params = 2;
		if (ioctl(fd, TEE_IOC_SUPPL_RECV, &rbd)) {
			if (errno == EINTR)
				continue;
			LOG(3, "TEE_IOC_SUPPL_RECV : %s", strerror(errno));
			return -1;
		}

		l = trouver(r.arg.func, (int)r.p[1].c);
		if (!l || r.p[1].a != 0 || r.p[1].b > l->taille) {
			LOG(3, "demande pour un listener inattendu 0x%x (mémoire %llu), refusée",
			    r.arg.func, (unsigned long long)r.p[1].c);
			ok = 0;
		} else {
			ok = l->servir(l) == 0;
		}

		memset(&s, 0, sizeof(s));
		s.arg.ret = ok ? 0 : 1;
		s.arg.num_params = 1;
		s.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;
		s.p[0].a = r.p[0].a;	/* génération de la requête */
		if (ioctl(fd, TEE_IOC_SUPPL_SEND, &sbd))
			LOG(3, "TEE_IOC_SUPPL_SEND (listener 0x%x) : %s", r.arg.func,
			    strerror(errno));
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct sigaction sa = { .sa_handler = sur_signal };
	uint32_t extra[MAX_LISTENERS];
	int nb_extra = 0, opt;
	rpmb_init_info_t info = { 0 };

	setvbuf(stderr, NULL, _IOLBF, 0);
	while ((opt = getopt(argc, argv, "wvne:")) != -1) {
		switch (opt) {
		case 'w':
			ecritures = 1;
			break;
		case 'v':
			qseecomd_verbeux++;
			break;
		case 'n':
			sans_rpmb = 1;
			break;
		case 'e':
			if (nb_extra < MAX_LISTENERS - 1)
				extra[nb_extra++] = strtoul(optarg, NULL, 0);
			break;
		default:
			fprintf(stderr, "usage : %s [-w] [-v] [-n] [-e ID]...\n", argv[0]);
			return 2;
		}
	}

	if (rpmb_init(&info) == 0)
		LOG(6, "RPMB UFS : %u secteurs de 512 o, %u trames par commande",
		    info.size, info.rel_wr_count);
	else
		LOG(4, "RPMB UFS introuvable : les demandes RPMB échoueront");
	LOG(6, "écritures RPMB %s", ecritures ? "AUTORISÉES" : "refusées (lecture seule)");

	if (ouvrir_priv())
		return 1;
	if (!sans_rpmb && enregistrer(RPMB_SERVICE_ID, RPMB_TAMPON, servir_rpmb))
		return 1;
	for (int i = 0; i < nb_extra; i++)
		enregistrer(extra[i], AUTRE_TAMPON, servir_journal);
	if (!nb_listeners) {
		LOG(3, "aucun listener enregistré");
		return 1;
	}

	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	notifier_systemd();

	boucle();
	/* fermer le périphérique retire les listeners côté TrustZone */
	close(fd);
	LOG(6, "arrêt");
	return 0;
}
