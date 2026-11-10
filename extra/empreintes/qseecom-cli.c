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
#include <time.h>
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
	if (s->fd < 0) {
		/*
		 * Souvent ENOMEM après des heures de marche : plus de bloc
		 * contigu assez grand (le tampon de 4 Mo). Remède : sync,
		 * drop_caches = 3, compact_memory = 1, ou redémarrer.
		 */
		fprintf(stderr, "  mémoire partagée de %zu octets refusée : %s\n",
			size, strerror(errno));
		return -1;
	}
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

/*
 * Clé HMAC scellée pour gfdit, obtenue de keymaster64 (0x205) tant que gfdit
 * est lui-même chargé en TrustZone : voir fetch_km_hmac_key() et le README
 * (section « Déblocage keymaster/0x205 »). 12 + longueur octets (statut,
 * décalage, longueur, puis la clé scellée), à recopier tels quels à l'octet
 * [28] de la commande 1000/10. NULL si l'appel échoue : la clé de test sert
 * alors de repli, mais sera rejetée par gfdit (statut 1012).
 */
static uint8_t *km_hmac_key;
static size_t km_hmac_len;

/*
 * Ouvre une session sur keymaster64 (sur le même fd client que gfdit, donc
 * en gardant gfdit chargé en parallèle) et envoie 0x205 (GET_AUTH_TOKEN_KEY).
 * TrustZone ne scelle le résultat pour gfdit que si gfdit est actuellement
 * chargé : sans ça, la commande renvoie un statut non nul (0xff000ffc
 * observé) sans erreur de transport.
 */
static uint8_t *fetch_km_hmac_key(int fd, size_t *out_len)
{
	struct {
		struct tee_ioctl_invoke_arg arg;
		struct tee_ioctl_param p[2];
	} b;
	struct tee_ioctl_buf_data bd = { .buf_ptr = (uintptr_t)&b, .buf_len = sizeof(b) };
	static const uint8_t req[] = { 0x05, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 };
	size_t req_len = 64, rsp_len = 256;
	struct shm s;
	uint32_t km_sess;
	uint32_t *r;
	size_t len;
	uint8_t *out;

	printf("  keymaster64 : récupération de la clé HMAC pour gfdit (0x205)\n");
	if (open_by_name(fd, "keymaster64", &km_sess)) {
		fprintf(stderr, "  keymaster64 : session refusée, clé HMAC réelle indisponible\n");
		return NULL;
	}
	if (shm_alloc(fd, req_len + rsp_len, &s))
		return NULL;
	memcpy(s.va, req, sizeof(req));

	memset(&b, 0, sizeof(b));
	b.arg.session = km_sess;
	b.arg.num_params = 2;
	b.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	b.p[0].b = req_len;
	b.p[0].c = s.id;
	b.p[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	b.p[1].a = req_len;
	b.p[1].b = rsp_len;
	b.p[1].c = s.id;
	if (ioctl(fd, TEE_IOC_INVOKE, &bd)) {
		fprintf(stderr, "  keymaster64 0x205 : %s\n", strerror(errno));
		return NULL;
	}
	r = (uint32_t *)(s.va + req_len);
	if (r[0] != 0) {
		fprintf(stderr, "  keymaster64 0x205 : statut 0x%x, gfdit n'est "
			"probablement pas chargé ; clé HMAC réelle indisponible\n", r[0]);
		return NULL;
	}
	len = r[2];
	out = malloc(12 + len);
	if (!out)
		return NULL;
	memcpy(out, s.va + req_len, 12 + len);
	*out_len = len;
	printf("  clé HMAC obtenue de keymaster64 (%zu octets scellés)\n", len);
	return out;
}

/*
 * Jeton d'authentification matériel (hw_auth_token_t d'Android, 69 octets)
 * pour ouvrir une session d'enrôlement (1000/3). Signé en HMAC-SHA256 avec la
 * clé que nous avons donnée à l'appli par 1000/10 (32 × 0x5a).
 */
static const uint8_t gf_hmac_key[32] = {
	0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
	0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
	0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
};
static uint64_t gf_challenge;

static const uint32_t sha_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256(const uint8_t *msg, size_t len, uint8_t out[32])
{
	uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	size_t total = ((len + 9 + 63) / 64) * 64;
	uint8_t *buf = calloc(1, total);

	memcpy(buf, msg, len);
	buf[len] = 0x80;
	for (int i = 0; i < 8; i++)
		buf[total - 1 - i] = (uint64_t)len * 8 >> (8 * i);
	for (size_t blk = 0; blk < total; blk += 64) {
		uint32_t w[64], a, b, c, d, e, f, g, hh;

		for (int i = 0; i < 16; i++)
			w[i] = buf[blk + 4 * i] << 24 | buf[blk + 4 * i + 1] << 16 |
			       buf[blk + 4 * i + 2] << 8 | buf[blk + 4 * i + 3];
		for (int i = 16; i < 64; i++)
			w[i] = w[i - 16] + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) +
			       w[i - 7] + (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10));
		a = h[0]; b = h[1]; c = h[2]; d = h[3];
		e = h[4]; f = h[5]; g = h[6]; hh = h[7];
		for (int i = 0; i < 64; i++) {
			uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) +
				      ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
			uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) +
				      ((a & b) ^ (a & c) ^ (b & c));

			hh = g; g = f; f = e; e = d + t1;
			d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d;
		h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
	free(buf);
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 4; j++)
			out[4 * i + j] = h[i] >> (24 - 8 * j);
}

static void hmac_sha256(const uint8_t key[32], const uint8_t *msg, size_t len,
			uint8_t out[32])
{
	uint8_t pad[64 + 64 + 32], inner[32];

	for (int i = 0; i < 64; i++)
		pad[i] = (i < 32 ? key[i] : 0) ^ 0x36;
	memcpy(pad + 64, msg, len);
	sha256(pad, 64 + len, inner);
	for (int i = 0; i < 64; i++)
		pad[i] = (i < 32 ? key[i] : 0) ^ 0x5c;
	memcpy(pad + 64, inner, 32);
	sha256(pad, 64 + 32, out);
}

static void put_be(uint8_t *p, uint64_t v, int n)
{
	for (int i = 0; i < n; i++)
		p[i] = v >> (8 * (n - 1 - i));
}

/* hw_auth_token_t : version, challenge, user_id, authenticator_id (hôte),
 * authenticator_type, timestamp (gros-boutistes), hmac sur les 37 premiers. */
static void gf_make_hat(uint8_t hat[69])
{
	uint64_t uid = getenv("GF_UID") ? strtoull(getenv("GF_UID"), NULL, 0) : 1;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	memset(hat, 0, 69);
	memcpy(hat + 1, &gf_challenge, 8);
	memcpy(hat + 9, &uid, 8);
	put_be(hat + 25, 1, 4);		/* HW_AUTH_PASSWORD */
	put_be(hat + 29, (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000, 8);
	hmac_sha256(gf_hmac_key, hat, 37, hat + 37);
}
#define GF_CARVEOUT_SIZE 0x400000

/*
 * Tampons partagés TEE, réutilisés pour toutes les commandes d'un même
 * processus plutôt que réalloués à chaque appel : TrustZone semble tenir,
 * par session, une table de taille fixe des enregistrements de mémoire
 * partagée qui ne se vide jamais avant la fermeture de la session (constaté
 * empiriquement : le 7e gf_cmd() d'une chaîne échoue toujours au niveau de
 * l'appel SMC lui-même — « scm call failed with error -22 » côté noyau —
 * quelle que soit la commande concernée, même après libération explicite
 * des tampons des appels précédents). Allouer une fois et réécrire leur
 * contenu reste sous cette limite.
 */
static struct shm hdr_buf, data_buf, cv_buf;
static bool hdr_ready, cv_ready;
static size_t data_cap;

static int gf_cmd(int fd, uint32_t session, uint32_t target, uint32_t cmd,
		  size_t size)
{
	struct {
		struct tee_ioctl_invoke_arg arg;
		struct tee_ioctl_param p[6];
	} b;
	struct tee_ioctl_buf_data bd = { .buf_ptr = (uintptr_t)&b, .buf_len = sizeof(b) };
	static uint32_t seq;
	/* Configuration du capteur rendue par 1002/2 (octets 32 à 480). */
	static uint8_t sensor_cfg[448];
	static bool have_cfg;
	uint32_t *h, *d;

	/*
	 * Comme le HAL avec un niveau de journal non nul : 32 Ko de plus après
	 * les données, où l'appli écrit son propre journal, et [24] = 1.
	 */
	size_t log_size = 0x8000;
	/*
	 * Le noyau range les tampons à la suite dans sa mémoire TZ : arrondir
	 * les données à la page pour que le tampon de 4 Mo qui suit commence
	 * sur une page (l'appli refuse sinon de l'enregistrer : statut 1012).
	 */
	size_t data_len = (size + log_size + 4095) & ~(size_t)4095;

	if (size < 12)
		return -ENOMEM;
	if (!hdr_ready) {
		if (shm_alloc(fd, 128, &hdr_buf))
			return -ENOMEM;
		hdr_ready = true;
	} else {
		memset(hdr_buf.va, 0, hdr_buf.size);
	}
	if (data_len > data_cap) {
		if (data_cap) {
			munmap(data_buf.va, data_buf.size);
			close(data_buf.fd);
		}
		if (shm_alloc(fd, data_len, &data_buf))
			return -ENOMEM;
		data_cap = data_len;
	} else {
		memset(data_buf.va, 0, data_len);
	}
#define hdr hdr_buf
#define data data_buf
#define cv cv_buf
	h = (uint32_t *)hdr.va;
	d = (uint32_t *)data.va;
	h[2] = size;		/* [8] */
	h[3] = ++seq;		/* [12] */
	h[6] = 1;		/* [24] niveau de journal */
	/*
	 * [28] : l'appli y écrit son statut, dans la requête et non dans la
	 * réponse. Le noyau (patch 0920) rend la requête quand elle est INOUT.
	 * Témoin 0xffffffff : s'il reste, l'appli n'a pas traité la commande
	 * (pas démarrée, ou paramètres refusés avant même de les lire).
	 */
	h[7] = 0xffffffff;

	/*
	 * GF_CMD_AUTH_SET_HMAC_KEY (1000/10) : Android y recopie, à partir de
	 * [28], la réponse de keymaster64 à la commande 0x205 (statut, offset de
	 * la clé, longueur, puis la clé). On y met une clé de test de 32 octets.
	 */
	if (target == 1000 && cmd == 10 && km_hmac_key && size >= 28 + 12 + km_hmac_len) {
		memcpy(data.va + 28, km_hmac_key, 12 + km_hmac_len);
	} else if (target == 1000 && cmd == 10 && size >= 28 + 12 + 32) {
		d[7] = 0;	/* [28] statut keymaster */
		d[8] = 12;	/* [32] offset de la clé, depuis [28] */
		d[9] = 32;	/* [36] longueur */
		memcpy(data.va + 40, gf_hmac_key, 32);
	}
	/*
	 * GF_CMD_ALGO_INIT (1001/0) : SZAlgo::init() envoie 180 octets nuls
	 * sauf [36] = 1. L'appli lit ce champ : ne pas le laisser à 0.
	 */
	if (target == 1001 && cmd == 0 && size >= 40)
		d[9] = 1;
	/*
	 * GF_CMD_SENSOR_CAPTURE_IMAGE (1002/3) : captureImage(op, n) met n en
	 * [28] et op en [32]. La capture d'usine (sz_factory_capture_multiple_frames)
	 * passe op = 7 : avec 1, le CRC des données brutes est faux (1028) ; avec 7,
	 * la capture réussit. GF_CAP_OP permet d'essayer une autre valeur.
	 */
	if (target == 1002 && cmd == 3 && size >= 36)
		d[8] = getenv("GF_CAP_OP") ? strtoul(getenv("GF_CAP_OP"), NULL, 0) : 7;
	/*
	 * GF_CMD_SENSOR_SET_DISPLAY (1002/6), comme
	 * CustomizedSensor::getDisplayType() : la configuration rendue par
	 * 1002/2 recopiée en [32], avec le type d'écran à cfg[388]
	 * (propriété vendor.panel.display, 5 par défaut ; GF_DISPLAY_TYPE).
	 */
	if (target == 1002 && cmd == 6 && size >= 32 + sizeof(sensor_cfg)) {
		uint32_t type = getenv("GF_DISPLAY_TYPE") ?
				strtoul(getenv("GF_DISPLAY_TYPE"), NULL, 0) : 5;

		if (!have_cfg)
			printf("  attention : pas de 1002/2 avant, configuration vide\n");
		memcpy(data.va + 32, sensor_cfg, sizeof(sensor_cfg));
		memcpy(data.va + 32 + 388, &type, 4);
	}
	/*
	 * GF_CMD_AUTH_SET_ACTIVE_GROUP (1000/0, 288 octets) : groupe en [28],
	 * chemin des gabarits en [32] (GF_PATH).
	 * GF_CMD_AUTH_ENROLL (1000/3, 108 octets) : groupe en [28], jeton
	 * d'authentification de 69 octets en [33] (challenge rendu par 1000/2).
	 */
	if (target == 1000 && cmd == 0 && size >= 288) {
		const char *path = getenv("GF_PATH") ? getenv("GF_PATH") : "/data/vendor_de/0/fpdata";

		d[7] = getenv("GF_GID") ? strtoul(getenv("GF_GID"), NULL, 0) : 0;
		strncpy((char *)data.va + 32, path, 255);
	}
	if (target == 1000 && cmd == 3 && size >= 102) {
		d[7] = getenv("GF_GID") ? strtoul(getenv("GF_GID"), NULL, 0) : 0;
		gf_make_hat(data.va + 33);
		printf("  jeton : challenge 0x%llx\n", (unsigned long long)gf_challenge);
	}
	/*
	 * GF_SET="1003/18@28=2000,1003/18@32=1" : mots de 32 bits à écrire dans
	 * les données de la commande MODULE/CMD, à l'octet indiqué, avant envoi.
	 */
	if (getenv("GF_SET")) {
		char *list = strdup(getenv("GF_SET")), *save, *tok;

		for (tok = strtok_r(list, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
			unsigned int m, c, off;
			unsigned long v;

			if (sscanf(tok, "%u/%u@%u=%li", &m, &c, &off, (long *)&v) == 4 &&
			    m == target && c == cmd && off + 4 <= size && off >= 12) {
				uint32_t w = v;

				memcpy(data.va + off, &w, 4);
				printf("  GF_SET : %u/%u [%u] = 0x%x\n", m, c, off, w);
			}
		}
		free(list);
	}
	d[1] = target;
	d[2] = cmd;

	memset(&b, 0, sizeof(b));
	b.arg.session = session;
	b.arg.num_params = 4;
	b.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
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
	b.p[3].b = data_len;
	b.p[3].c = data.id;
	if (carveout) {
		if (!cv_ready) {
			if (shm_alloc(fd, GF_CARVEOUT_SIZE, &cv))
				return -ENOMEM;
			cv_ready = true;
		} else {
			memset(cv.va, 0, GF_CARVEOUT_SIZE);
		}
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
	if (target == 1000 && cmd == 2 && size >= 40 && !h[7]) {
		memcpy(&gf_challenge, data.va + 32, 8);
		printf("  challenge : 0x%llx\n", (unsigned long long)gf_challenge);
	}
	/*
	 * gf_sensor_init() tolère le code 0x3e9 (1001) comme un cas non fatal
	 * en interne (même branche que le succès, pas de reset_flag posé) mais
	 * le remonte quand même tel quel à l'appelant au lieu de le convertir
	 * en 0. On met donc en cache la config dans ce cas aussi, sinon elle
	 * ne l'est jamais et 1002/6 ne réinjecte qu'une config vide.
	 */
	if (target == 1002 && cmd == 2 && size >= 32 + sizeof(sensor_cfg) &&
	    (!h[7] || h[7] == 0x3e9)) {
		memcpy(sensor_cfg, data.va + 32, sizeof(sensor_cfg));
		have_cfg = true;
	}
	printf("  goodix %u/%u (%zu octets) -> ret=0x%x, statut TA (requête[28])=0x%x (%d), "
	       "erreur module=%u, résultat=0x%x\n",
	       target, cmd, size, b.arg.ret, h[7], (int)h[7], d[3], d[0]);
	if (h[7] == 0xffffffff)
		printf("  témoin intact : commande non traitée (appli pas démarrée ?) "
		       "ou noyau sans le patch 0920\n");
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

	/*
	 * GF_DUMP=préfixe : données complètes de chaque commande dans
	 * préfixe-MODULE-CMD-SEQ.bin (images de test, configuration...).
	 */
	if (getenv("GF_DUMP")) {
		char name[256];
		FILE *f;

		snprintf(name, sizeof(name), "%s-%u-%u-%u.bin", getenv("GF_DUMP"),
			 target, cmd, seq);
		f = fopen(name, "wb");
		if (f) {
			fwrite(data.va, 1, size, f);
			fclose(f);
			printf("  données complètes : %s (%zu octets)\n", name, size);
		} else {
			perror(name);
		}
	}
	/*
	 * hdr_buf/data_buf/cv_buf sont volontairement NON libérés ici : ils
	 * sont réutilisés par le prochain gf_cmd() de ce même processus (voir
	 * le commentaire au-dessus de leur déclaration). Le noyau les libère
	 * à la fermeture des descripteurs, à la sortie du processus.
	 */
	return 0;
#undef hdr
#undef data
#undef cv
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

	if (goodix && !strcmp(argv[1], "gfdit"))
		km_hmac_key = fetch_km_hmac_key(cli, &km_hmac_len);

	if (goodix)
		for (int i = 3; i + 2 < argc; i += 3) {
			unsigned long m = strtoul(argv[i], NULL, 0);

			/* « 0 0 MS » : pause de MS millisecondes (attendre l'IRQ). */
			if (m == 0) {
				unsigned long ms = strtoul(argv[i + 2], NULL, 0);

				printf("  pause de %lu ms\n", ms);
				fflush(stdout);
				usleep(ms * 1000);
				continue;
			}
			gf_cmd(cli, sess, m, strtoul(argv[i + 1], NULL, 0),
			       strtoul(argv[i + 2], NULL, 0));
		}
	else
		for (int i = 2; i + 1 < argc; i += 2)
			send_cmd(cli, sess, argv[i], strtoul(argv[i + 1], NULL, 0));

	printf("Entrée pour décharger l'appli et quitter.\n");
	getchar();
	return 0;
}
