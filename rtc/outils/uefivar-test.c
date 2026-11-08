// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * uefivar-test : interroger l'appli TrustZone qcom.tz.uefisecapp (variables
 * UEFI, partition uefivarstore) depuis Linux, via le pilote TEE « qseecom »
 * (impl_id 5, patch 0918). Aucune modification du noyau. Lecture seule sauf
 * avec -w / -d.
 *
 * But : savoir si le décalage du RTC peut être gardé en TrustZone, dans la
 * variable RTCInfo qu'utilise rtc-pm8xxx avec « qcom,uefi-rtc-info ».
 *
 *   uefivar-test          espace des variables, RTCInfo, variable de test
 *   uefivar-test -l       idem, puis liste de toutes les variables
 *   uefivar-test -w       ÉCRIT la variable de test DitingTest (heure actuelle)
 *   uefivar-test -d       EFFACE la variable de test
 *
 * La variable de test a son propre GUID (d17b6a3e-5c2f-4f0e-9a41-7d3c2b8e6f10) :
 * elle ne touche ni RTCInfo ni les variables du bootloader. Elle sert à voir si
 * une écriture depuis Linux survit à un redémarrage.
 *
 * Commandes de uefisecapp (protocole de drivers/firmware/qcom/
 * qcom_qseecom_uefisecapp.c) : 0x8000 GetVariable, 0x8001 SetVariable
 * (seulement avec -w / -d, et seulement sur DitingTest), 0x8002
 * GetNextVariableName, 0x8003 QueryVariableInfo.
 *
 * Compilation : cc -O2 -Wall -o uefivar-test uefivar-test.c
 */
#include <errno.h>
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

#define APP		"qcom.tz.uefisecapp"
#define CMD_GET_VAR	0x8000
#define CMD_SET_VAR	0x8001
#define CMD_GET_NEXT	0x8002
#define CMD_QUERY_INFO	0x8003

#define REQ_MAX		1024	/* requête, puis réponse à cet offset */
#define RSP_MAX		1024

/* GUID de RTCInfo (rtc-pm8xxx), au format EFI : 3 champs petit-boutistes */
static const uint8_t rtc_guid[16] = {
	0x2b, 0x8c, 0x2f, 0x88, 0x46, 0x96, 0x5f, 0x43,
	0x8d, 0xe5, 0xf2, 0x08, 0xff, 0x80, 0xc1, 0xbd,
};

/* variable de test, GUID propre à ce dépôt */
static const uint8_t test_guid[16] = {
	0x3e, 0x6a, 0x7b, 0xd1, 0x2f, 0x5c, 0x0e, 0x4f,
	0x9a, 0x41, 0x7d, 0x3c, 0x2b, 0x8e, 0x6f, 0x10,
};
#define TEST_NAME	"DitingTest"

static int fd = -1;
static uint32_t session;
static uint8_t *buf;
static int shm_id;

static int ouvrir(void)
{
	char path[32];

	for (int i = 0; i < 8; i++) {
		struct tee_ioctl_version_data v;

		snprintf(path, sizeof(path), "/dev/tee%d", i);
		fd = open(path, O_RDWR);
		if (fd < 0)
			continue;
		if (!ioctl(fd, TEE_IOC_VERSION, &v) && v.impl_id == TEE_IMPL_ID_QSEECOM) {
			printf("périphérique TEE qseecom : %s\n", path);
			return 0;
		}
		close(fd);
	}
	fprintf(stderr, "aucun /dev/teeN qseecom (module qcom_qseecom_tee chargé ? lancer avec sudo)\n");
	return -1;
}

static int shm(void)
{
	struct tee_ioctl_shm_alloc_data d = { .size = REQ_MAX + RSP_MAX };
	int sfd = ioctl(fd, TEE_IOC_SHM_ALLOC, &d);

	if (sfd < 0)
		return -1;
	shm_id = d.id;
	buf = mmap(NULL, d.size, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
	return buf == MAP_FAILED ? -1 : 0;
}

static int session_app(void)
{
	struct {
		struct tee_ioctl_open_session_arg arg;
		struct tee_ioctl_param p[1];
	} b;
	struct tee_ioctl_buf_data bd = { .buf_ptr = (uintptr_t)&b, .buf_len = sizeof(b) };
	size_t len = strlen(APP) + 1;

	memset(buf, 0, REQ_MAX + RSP_MAX);
	memcpy(buf, APP, len);
	memset(&b, 0, sizeof(b));
	b.arg.num_params = 1;
	b.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	b.p[0].b = len;
	b.p[0].c = shm_id;
	if (ioctl(fd, TEE_IOC_OPEN_SESSION, &bd)) {
		perror("open_session");
		return -1;
	}
	if (b.arg.ret) {
		fprintf(stderr, "session %s refusée : ret=0x%x origine=%u\n",
			APP, b.arg.ret, b.arg.ret_origin);
		return -1;
	}
	session = b.arg.session;
	printf("session ouverte sur %s (appli chargée par le bootloader)\n", APP);
	return 0;
}

/* requête déjà écrite à buf[0..req_len) ; réponse lue à buf[REQ_MAX..] */
static int envoyer(size_t req_len, size_t rsp_len)
{
	struct {
		struct tee_ioctl_invoke_arg arg;
		struct tee_ioctl_param p[2];
	} b;
	struct tee_ioctl_buf_data bd = { .buf_ptr = (uintptr_t)&b, .buf_len = sizeof(b) };

	memset(buf + REQ_MAX, 0, RSP_MAX);
	memset(&b, 0, sizeof(b));
	b.arg.session = session;
	b.arg.num_params = 2;
	b.p[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	b.p[0].b = req_len;
	b.p[0].c = shm_id;
	b.p[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	b.p[1].a = REQ_MAX;
	b.p[1].b = rsp_len;
	b.p[1].c = shm_id;
	if (ioctl(fd, TEE_IOC_INVOKE, &bd)) {
		perror("invoke");
		return -1;
	}
	if (b.arg.ret) {
		fprintf(stderr, "invoke : ret=0x%x origine=%u\n", b.arg.ret, b.arg.ret_origin);
		return -1;
	}
	return 0;
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

static const char *efi_statut(uint32_t s)
{
	switch (s) {
	case 0: return "succès";
	case 0x80000005: return "tampon trop petit";
	case 0x80000002: return "paramètre invalide";
	case 0x80000003: return "non pris en charge";
	case 0x80000007: return "erreur matérielle";
	case 0x8000000e: return "introuvable";
	case 0x80000009: return "plus de place";
	default: return "?";
	}
}

static void query_info(void)
{
	memset(buf, 0, REQ_MAX);
	wr32(buf + 0, CMD_QUERY_INFO);
	wr32(buf + 4, 12);
	wr32(buf + 8, 0x7);		/* NON_VOLATILE | BOOTSERVICE | RUNTIME */
	if (envoyer(16, 48))
		return;
	uint8_t *r = buf + REQ_MAX;
	printf("\nQueryVariableInfo : commande 0x%x, statut 0x%x (%s)\n",
	       rd32(r), rd32(r + 8), efi_statut(rd32(r + 8)));
	if (!rd32(r + 8))
		printf("  espace total %llu o, libre %llu o, variable max %llu o\n",
		       (unsigned long long)rd64(r + 16), (unsigned long long)rd64(r + 24),
		       (unsigned long long)rd64(r + 32));
}

static size_t ucs2(uint8_t *dst, const char *s)
{
	size_t n = 0;

	for (; *s; s++, n += 2) {
		dst[n] = (uint8_t)*s;
		dst[n + 1] = 0;
	}
	dst[n] = dst[n + 1] = 0;
	return n + 2;
}

/* GetVariable ; renvoie le statut EFI, données copiées dans out (64 o max) */
static uint32_t get_var(const uint8_t guid[16], const char *nom, uint32_t *attr,
			uint8_t *out, uint32_t *len)
{
	/* en-tête 28 o, GUID à 32, nom à 48 */
	size_t name_len;

	memset(buf, 0, REQ_MAX);
	memcpy(buf + 32, guid, 16);
	name_len = ucs2(buf + 48, nom);
	wr32(buf + 0, CMD_GET_VAR);
	wr32(buf + 4, 48 + name_len);
	wr32(buf + 8, 48);		/* name_offset */
	wr32(buf + 12, name_len);
	wr32(buf + 16, 32);		/* guid_offset */
	wr32(buf + 20, 16);
	wr32(buf + 24, 64);		/* data_size proposée */
	if (envoyer(48 + name_len, 24 + 64))
		return 0xffffffff;
	uint8_t *r = buf + REQ_MAX;
	uint32_t st = rd32(r + 8);

	if (!st) {
		uint32_t off = rd32(r + 16);

		*attr = rd32(r + 12);
		*len = rd32(r + 20);
		if (*len > 64 || off + *len > RSP_MAX)
			*len = 0;
		memcpy(out, r + off, *len);
	}
	return st;
}

static void afficher_var(const char *titre, const uint8_t guid[16], const char *nom)
{
	uint8_t d[64];
	uint32_t attr, len, st = get_var(guid, nom, &attr, d, &len);

	printf("\n%s : statut 0x%x (%s)\n", titre, st, efi_statut(st));
	if (st)
		return;
	printf("  attributs 0x%x, %u octets :", attr, len);
	for (uint32_t i = 0; i < len; i++)
		printf(" %02x", d[i]);
	printf("\n");
	if (guid == rtc_guid && len >= 4)
		printf("  décalage GPS %u s\n", rd32(d));
	if (guid == test_guid && len >= 8) {
		time_t t = (time_t)rd64(d);

		printf("  écrite le %s", ctime(&t));
	}
}

/* SetVariable sur DitingTest uniquement ; data NULL = effacement */
static void set_test(const uint8_t *data, uint32_t len)
{
	/* en-tête 36 o, GUID à 40, nom à 56, données à 128 */
	size_t name_len;

	memset(buf, 0, REQ_MAX);
	memcpy(buf + 40, test_guid, 16);
	name_len = ucs2(buf + 56, TEST_NAME);
	if (len)
		memcpy(buf + 128, data, len);
	wr32(buf + 0, CMD_SET_VAR);
	wr32(buf + 4, 128 + len);
	wr32(buf + 8, 56);		/* name_offset */
	wr32(buf + 12, name_len);
	wr32(buf + 16, 40);		/* guid_offset */
	wr32(buf + 20, 16);
	wr32(buf + 24, 0x7);		/* NON_VOLATILE | BOOTSERVICE | RUNTIME */
	wr32(buf + 28, 128);		/* data_offset */
	wr32(buf + 32, len);
	if (envoyer(128 + (len ? len : 8), 32))
		return;
	uint32_t st = rd32(buf + REQ_MAX + 8);

	printf("\nSetVariable %s (%s) : statut 0x%x (%s)\n", TEST_NAME,
	       len ? "écriture" : "effacement", st, efi_statut(st));
}

static void lister(void)
{
	uint8_t guid[16] = { 0 };
	uint8_t name[512] = { 0 };	/* nom vide = première variable */
	int n = 0;

	printf("\nvariables :\n");
	for (;;) {
		memset(buf, 0, REQ_MAX);
		memcpy(buf + 24, guid, 16);
		memcpy(buf + 40, name, sizeof(name));
		wr32(buf + 0, CMD_GET_NEXT);
		wr32(buf + 4, 40 + sizeof(name));
		wr32(buf + 8, 24);		/* guid_offset */
		wr32(buf + 12, 16);
		wr32(buf + 16, 40);		/* name_offset */
		wr32(buf + 20, sizeof(name));
		if (envoyer(40 + sizeof(name), 28 + 16 + sizeof(name) + 4))
			return;
		uint8_t *r = buf + REQ_MAX;
		uint32_t st = rd32(r + 8);

		if (st) {
			printf("  (fin : statut 0x%x %s) %d variables\n", st, efi_statut(st), n);
			return;
		}
		uint32_t go = rd32(r + 12), no = rd32(r + 20), nl = rd32(r + 24);

		if (no + nl > RSP_MAX || go + 16 > RSP_MAX || nl > sizeof(name)) {
			printf("  réponse incohérente, arrêt\n");
			return;
		}
		memcpy(guid, r + go, 16);
		memset(name, 0, sizeof(name));
		memcpy(name, r + no, nl);
		printf("  {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x} ",
		       rd32(guid), guid[4] | guid[5] << 8, guid[6] | guid[7] << 8,
		       guid[8], guid[9], guid[10], guid[11], guid[12], guid[13],
		       guid[14], guid[15]);
		for (uint32_t i = 0; i + 1 < nl && name[i]; i += 2)
			putchar(name[i] < 0x80 ? name[i] : '?');
		putchar('\n');
		if (++n > 500) {
			printf("  plus de 500, arrêt\n");
			return;
		}
	}
}

int main(int argc, char **argv)
{
	if (ouvrir() || shm() || session_app())
		return 1;
	if (argc > 1 && !strcmp(argv[1], "-w")) {
		uint8_t d[8];
		uint64_t t = (uint64_t)time(NULL);

		memcpy(d, &t, 8);
		set_test(d, 8);
	} else if (argc > 1 && !strcmp(argv[1], "-d")) {
		set_test(NULL, 0);
	}
	query_info();
	afficher_var("RTCInfo {882f8c2b-9646-435f-8de5-f208ff80c1bd}", rtc_guid, "RTCInfo");
	afficher_var("DitingTest {d17b6a3e-5c2f-4f0e-9a41-7d3c2b8e6f10}", test_guid, TEST_NAME);
	if (argc > 1 && !strcmp(argv[1], "-l"))
		lister();
	return 0;
}
