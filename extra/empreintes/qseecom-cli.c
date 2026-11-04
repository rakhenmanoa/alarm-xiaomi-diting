// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * qseecom-cli : charger une appli QSEE et lui envoyer des commandes brutes,
 * via le pilote TEE « qseecom » (impl_id 5) de samcday/linux.
 *
 *   qseecom-cli NOM                     charge NOM (NOM.mdt + .bNN dans
 *                                       /usr/lib/firmware) et ouvre une session
 *   qseecom-cli NOM HEXREQ RSPLEN [...] idem, puis envoie chaque requête
 *                                       (octets en hexadécimal) et affiche les
 *                                       RSPLEN premiers octets de la réponse
 *
 * Exemple : qseecom-cli gfdit 01000000 64
 *
 * L'appli reste chargée tant que ce programme tourne (le pilote la décharge
 * à la fermeture de la dernière session) : il attend Entrée avant de quitter.
 * Il ne sert aucun « listener » : une appli qui en demande un restera
 * bloquée jusqu'au délai du pilote.
 *
 * Compilation : cc -O2 -Wall -o qseecom-cli qseecom-cli.c
 */
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/tee.h>

#ifndef TEE_IMPL_ID_QSEECOM
#define TEE_IMPL_ID_QSEECOM 5
#endif

struct shm {
	int fd;
	int id;
	size_t size;
	uint8_t *va;
};

static int find_dev(const char *prefix, char *path, size_t len)
{
	for (int i = 0; i < 8; i++) {
		struct tee_ioctl_version_data v;
		int fd;

		snprintf(path, len, "/dev/%s%d", prefix, i);
		fd = open(path, O_RDWR);
		if (fd < 0)
			continue;
		if (!ioctl(fd, TEE_IOC_VERSION, &v) && v.impl_id == TEE_IMPL_ID_QSEECOM)
			return fd;
		close(fd);
	}
	return -1;
}

static int shm_alloc(int fd, size_t size, struct shm *s)
{
	struct tee_ioctl_shm_alloc_data d = { .size = size };

	s->fd = ioctl(fd, TEE_IOC_SHM_ALLOC, &d);
	if (s->fd < 0)
		return -1;
	s->id = d.id;
	s->size = d.size;
	s->va = mmap(NULL, s->size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
	if (s->va == MAP_FAILED)
		return -1;
	memset(s->va, 0, s->size);
	return 0;
}

/* Session ouverte avec le nom de l'appli en paramètre 0 (memref). */
static int open_by_name(int fd, const char *name, uint32_t *session)
{
	struct {
		struct tee_ioctl_open_session_arg arg;
		struct tee_ioctl_param p[1];
	} b;
	struct tee_ioctl_buf_data bd = { .buf_ptr = (uintptr_t)&b, .buf_len = sizeof(b) };
	size_t len = strlen(name) + 1;
	struct shm s;

	if (shm_alloc(fd, len, &s))
		return -errno;
	memcpy(s.va, name, len);

	memset(&b, 0, sizeof(b));
	b.arg.num_params = 1;
	b.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	b.p[0].a = 0;
	b.p[0].b = len;
	b.p[0].c = s.id;
	if (ioctl(fd, TEE_IOC_OPEN_SESSION, &bd))
		return -errno;
	fprintf(stderr, "  open_session : ret=0x%x origine=%u session=%u\n",
		b.arg.ret, b.arg.ret_origin, b.arg.session);
	if (b.arg.ret)
		return -EIO;
	*session = b.arg.session;
	return 0;
}

static int hex2bin(const char *h, uint8_t *out, size_t max)
{
	size_t n = strlen(h);

	if (n % 2 || n / 2 > max)
		return -1;
	for (size_t i = 0; i < n / 2; i++) {
		unsigned int v;

		if (sscanf(h + 2 * i, "%2x", &v) != 1)
			return -1;
		out[i] = v;
	}
	return n / 2;
}

/* Requête et réponse dans le même tampon, la réponse après la requête. */
static int send_cmd(int fd, uint32_t session, const char *hex, size_t rsp_len)
{
	struct {
		struct tee_ioctl_invoke_arg arg;
		struct tee_ioctl_param p[2];
	} b;
	struct tee_ioctl_buf_data bd = { .buf_ptr = (uintptr_t)&b, .buf_len = sizeof(b) };
	size_t req_len = (strlen(hex) / 2 + 63) & ~(size_t)63;
	struct shm s;
	int n;

	if (shm_alloc(fd, req_len + rsp_len, &s))
		return -errno;
	n = hex2bin(hex, s.va, req_len);
	if (n < 0) {
		fprintf(stderr, "requête hexadécimale invalide : %s\n", hex);
		return -EINVAL;
	}

	memset(&b, 0, sizeof(b));
	b.arg.session = session;
	b.arg.num_params = 2;
	b.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	b.p[0].a = 0;
	b.p[0].b = req_len;
	b.p[0].c = s.id;
	b.p[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	b.p[1].a = req_len;
	b.p[1].b = rsp_len;
	b.p[1].c = s.id;
	if (ioctl(fd, TEE_IOC_INVOKE, &bd)) {
		fprintf(stderr, "  invoke : %s\n", strerror(errno));
		return -errno;
	}
	printf("  requête %s -> ret=0x%x origine=%u\n  réponse :", hex,
	       b.arg.ret, b.arg.ret_origin);
	for (size_t i = 0; i < rsp_len; i++)
		printf("%s%02x", i % 16 ? " " : "\n    ", s.va[req_len + i]);
	printf("\n");
	return 0;
}

/*
 * Commande Goodix (protocole de libgf_hal.so, CaEntry::sendCommand) :
 * requête de 64 octets, réponse de 64 octets, et un tampon de données
 * séparé dont l'adresse physique est écrite par le noyau à l'offset 0 de la
 * requête (4 octets). Requête : [8] taille des données, [12] compteur,
 * [16] horodatage, [24] niveau de journal, [32] adresse du tampon sécurisé
 * de 4 Mo (laissée à 0 ici), [40] taille associée. Réponse : [28] statut.
 * Données : [0] résultat, [4] module (1000 auth, 1001 algo, 1002 capteur,
 * 1003 test, 1004 dump), [8] commande.
 */
/*
 * Avec --goodix-carveout, un tampon ordinaire de 4 Mo tient lieu du tampon
 * sécurisé d'Android (tas qcom,secure-cdsp) : adresse en [32], taille en
 * [40]. L'appli peut l'exiger dans de la mémoire réservée au CDSP.
 */
static bool carveout;
#define GF_CARVEOUT_SIZE 0x400000

static int gf_cmd(int fd, uint32_t session, uint32_t target, uint32_t cmd,
		  size_t size)
{
	struct {
		struct tee_ioctl_invoke_arg arg;
		struct tee_ioctl_param p[6];
	} b;
	struct shm cv;
	struct tee_ioctl_buf_data bd = { .buf_ptr = (uintptr_t)&b, .buf_len = sizeof(b) };
	static uint32_t seq;
	struct shm hdr, data;
	uint32_t *h, *d;

	/*
	 * Comme le HAL avec un niveau de journal non nul : 32 Ko de plus après
	 * les données, où l'appli écrit son propre journal, et [24] = 1.
	 */
	size_t log_size = 0x8000;

	if (size < 12 || shm_alloc(fd, 128, &hdr) ||
	    shm_alloc(fd, size + log_size, &data))
		return -ENOMEM;
	h = (uint32_t *)hdr.va;
	d = (uint32_t *)data.va;
	h[2] = size;		/* [8] */
	h[3] = ++seq;		/* [12] */
	h[6] = 1;		/* [24] niveau de journal */

	/*
	 * GF_CMD_AUTH_SET_HMAC_KEY (1000/10) : Android y recopie, à partir de
	 * [28], la réponse de keymaster64 à la commande 0x205 (statut, offset de
	 * la clé, longueur, puis la clé). On y met une clé de test de 32 octets.
	 */
	if (target == 1000 && cmd == 10 && size >= 28 + 12 + 32) {
		d[7] = 0;	/* [28] statut keymaster */
		d[8] = 12;	/* [32] offset de la clé, depuis [28] */
		d[9] = 32;	/* [36] longueur */
		memset(data.va + 40, 0x5a, 32);
	}
	d[1] = target;
	d[2] = cmd;

	memset(&b, 0, sizeof(b));
	b.arg.session = session;
	b.arg.num_params = 4;
	b.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	b.p[0].b = 64;
	b.p[0].c = hdr.id;
	b.p[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	b.p[1].a = 64;
	b.p[1].b = 64;
	b.p[1].c = hdr.id;
	b.p[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	b.p[2].a = 0;		/* offset dans la requête */
	b.p[2].b = 4;		/* largeur de l'adresse */
	b.p[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	b.p[3].b = size + log_size;
	b.p[3].c = data.id;
	if (carveout) {
		if (shm_alloc(fd, GF_CARVEOUT_SIZE, &cv))
			return -ENOMEM;
		h[10] = GF_CARVEOUT_SIZE;	/* [40] */
		b.arg.num_params = 6;
		b.p[4].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
		b.p[4].a = 32;
		b.p[4].b = 4;
		b.p[5].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
		b.p[5].b = GF_CARVEOUT_SIZE;
		b.p[5].c = cv.id;
	}
	/* Le noyau exige la taille exacte : en-tête + paramètres utilisés. */
	bd.buf_len = sizeof(b.arg) + b.arg.num_params * sizeof(b.p[0]);
	if (ioctl(fd, TEE_IOC_INVOKE, &bd)) {
		fprintf(stderr, "  invoke : %s\n", strerror(errno));
		return -errno;
	}
	printf("  goodix %u/%u (%zu octets) -> ret=0x%x, statut TA=0x%x, résultat=0x%x\n",
	       target, cmd, size, b.arg.ret, ((uint32_t *)(hdr.va + 64))[7], d[0]);
	printf("  réponse (64 octets) :");
	for (int i = 0; i < 64; i++)
		printf("%s%02x", i % 16 ? " " : "\n    ", hdr.va[64 + i]);
	printf("\n  données :");
	for (size_t i = 0; i < size && i < 256; i++)
		printf("%s%02x", i % 16 ? " " : "\n    ", data.va[i]);
	printf("\n  texte :");
	for (size_t i = 12; i < size; i++) {
		uint8_t c = data.va[i];

		if (c >= 32 && c < 127)
			putchar(c);
		else if (c == 0 && i + 1 < size && data.va[i + 1] >= 32 && data.va[i + 1] < 127)
			printf(" | ");
	}
	if (carveout) {
		/* Zones non nulles du tampon de 4 Mo, par blocs de 16 octets. */
		size_t shown = 0, last = (size_t)-32;

		printf("\n  tampon de 4 Mo (zones non nulles) :");
		for (size_t i = 0; i + 16 <= GF_CARVEOUT_SIZE && shown < 64; i += 16) {
			static const uint8_t zero[16];

			if (!memcmp(cv.va + i, zero, 16))
				continue;
			if (i != last + 16)
				printf("\n    ...");
			printf("\n    %06zx:", i);
			for (int j = 0; j < 16; j++)
				printf(" %02x", cv.va[i + j]);
			last = i;
			shown++;
		}
		if (!shown)
			printf(" aucune");
	}
	printf("\n  journal de l'appli :\n");
	for (size_t i = size; i < size + log_size; i++) {
		uint8_t c = data.va[i];

		if (c >= 32 && c < 127)
			putchar(c);
		else if (c == '\n' || (c == 0 && i > size && data.va[i - 1] >= 32))
			putchar('\n');
	}
	printf("\n");
	return 0;
}

int main(int argc, char **argv)
{
	char priv_path[32], cli_path[32];
	uint32_t load_sess, sess;
	int priv, cli, ret;
	bool goodix = argc >= 3 && (!strcmp(argv[2], "--goodix") ||
				    !strcmp(argv[2], "--goodix-carveout"));

	carveout = goodix && !strcmp(argv[2], "--goodix-carveout");

	if (argc < 2 || (!goodix && (argc - 2) % 2) || (goodix && (argc - 3) % 3)) {
		fprintf(stderr, "usage : %s NOM [HEXREQ RSPLEN]...\n"
			"        %s NOM --goodix[-carveout] MODULE COMMANDE TAILLE [...]\n",
			argv[0], argv[0]);
		return 2;
	}

	priv = find_dev("teepriv", priv_path, sizeof(priv_path));
	cli = find_dev("tee", cli_path, sizeof(cli_path));
	if (priv < 0 || cli < 0) {
		fprintf(stderr, "pas de périphérique TEE qseecom (module qseecomtee chargé ? root ?)\n");
		return 1;
	}
	printf("qseecom : %s (privilégié), %s (client)\n", priv_path, cli_path);

	printf("chargement de %s\n", argv[1]);
	ret = open_by_name(priv, argv[1], &load_sess);
	if (ret)
		fprintf(stderr, "chargement refusé : %s ; essai d'une session sur une "
			"appli déjà chargée\n", strerror(-ret));

	printf("session client sur %s\n", argv[1]);
	ret = open_by_name(cli, argv[1], &sess);
	if (ret) {
		fprintf(stderr, "session refusée : %s (voir dmesg)\n", strerror(-ret));
		return 1;
	}
	printf("appli chargée et session ouverte\n");

	if (goodix)
		for (int i = 3; i + 2 < argc; i += 3)
			gf_cmd(cli, sess, strtoul(argv[i], NULL, 0),
			       strtoul(argv[i + 1], NULL, 0),
			       strtoul(argv[i + 2], NULL, 0));
	else
		for (int i = 2; i + 1 < argc; i += 2)
			send_cmd(cli, sess, argv[i], strtoul(argv[i + 1], NULL, 0));

	printf("Entrée pour décharger l'appli et quitter.\n");
	getchar();
	return 0;
}
