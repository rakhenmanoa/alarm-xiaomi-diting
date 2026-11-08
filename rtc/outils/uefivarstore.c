// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * uefivarstore : lire la partition uefivarstore du diting et y écrire RTCInfo
 * directement, comme le fait le bootloader.
 *
 * Pourquoi : sur ce firmware, uefisecapp tient les variables non volatiles en
 * mémoire seulement à l'exécution (SetVariable et la commande de
 * synchronisation 0x800b répondent « succès » sans rien écrire). C'est le
 * bootloader qui lit et réécrit la partition à chaque démarrage. Une valeur
 * écrite ici est donc chargée dans uefisecapp au démarrage suivant.
 *
 * Format (relevé sur le téléphone, voir rtc/README.md section 8) :
 *   bloc 0 : « PTBL », u32 version, u32 taille, u32 nombre de tables, puis
 *            16 octets par table : u32 type, u16 premier bloc, u16 nb blocs ;
 *   table  : deux bancs de taille égale (moitié basse, moitié haute) ;
 *   banc   : bloc d'en-tête « DStr », nblk blocs « VAR2 », bloc d'en-tête copié ;
 *   DStr   : +6 u8 numéro de banc (2 = moitié basse, 1 = moitié haute),
 *            +0x0c u16 nblk, +0x0e u16 blocs utilisés, +0x10 u32 génération,
 *            +0x14 u32 CRC32 des données (longueur donnée par VAR2),
 *            +0x28 u32 CRC32 des 0x28 premiers octets ;
 *   VAR2   : +8 u32 longueur utilisée ; groupes dès +0x14 :
 *            u32 taille, GUID, puis variables : u32 taille, 8 octets,
 *            u32 taille des données, u16 taille du nom, nom UCS-2, données
 *            alignées sur 4.
 * Le banc valide de plus haute génération fait foi. On écrit le nouveau banc
 * (génération + 1) sur l'autre, dans l'ordre du code de uefisecapp
 * (DataMgrWrite) : en-tête, données, copie de l'en-tête.
 *
 *   uefivarstore [-f FICHIER] liste
 *   uefivarstore [-f FICHIER] rtcinfo GPS     (GPS : décalage, origine 1980)
 *   uefivarstore [-f FICHIER] rtcinfo-unix UNIX
 *   uefivarstore [-f FICHIER] rtcinfo-auto   (heure système - compteur RTC)
 *
 * Par défaut : /dev/disk/by-partlabel/uefivarstore. Avant toute écriture sur
 * la partition, une copie complète est faite dans le dossier courant.
 *
 * Compilation : cc -O2 -Wall -o uefivarstore uefivarstore.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BLOC		4096
#define TAILLE_MAX	(4 << 20)
#define GPS_EPOCH	315964800LL

static const uint8_t rtc_guid[16] = {
	0x2b, 0x8c, 0x2f, 0x88, 0x46, 0x96, 0x5f, 0x43,
	0x8d, 0xe5, 0xf2, 0x08, 0xff, 0x80, 0xc1, 0xbd,
};

static uint8_t *img;
static size_t taille;

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

static uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t c = 0xffffffff;

	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320 & -(c & 1));
	}
	return ~c;
}

struct banc {
	size_t debut;		/* octet de l'en-tête */
	int valide;
	uint32_t gen;
	uint16_t nblk, utilises;
	uint32_t var2_len;
};

struct table {
	uint32_t type;
	uint32_t premier, nb;	/* en blocs */
	struct banc b[2];	/* 0 : moitié basse, 1 : moitié haute */
};

static struct table tables[8];
static int nb_tables;

static void lire_banc(struct banc *b, size_t debut, uint32_t nblk_attendu)
{
	const uint8_t *h = img + debut;

	memset(b, 0, sizeof(*b));
	b->debut = debut;
	if (debut + (nblk_attendu + 2) * BLOC > taille || memcmp(h, "DStr", 4))
		return;
	b->nblk = rd16(h + 0x0c);
	b->utilises = rd16(h + 0x0e);
	b->gen = rd32(h + 0x10);
	if (b->nblk != nblk_attendu || !b->utilises || b->utilises > b->nblk)
		return;
	if (crc32(h, 0x28) != rd32(h + 0x28))
		return;
	/* copie de l'en-tête après les données */
	if (memcmp(h, img + debut + (1 + b->nblk) * BLOC, 0x30))
		return;
	const uint8_t *v = h + BLOC;

	if (memcmp(v, "VAR2", 4))
		return;
	b->var2_len = rd32(v + 8);
	if (b->var2_len < 0x14 || b->var2_len > (uint32_t)b->utilises * BLOC)
		return;
	if (crc32(v, b->var2_len) != rd32(h + 0x14))
		return;
	b->valide = 1;
}

static int lire_ptbl(void)
{
	if (taille < BLOC || memcmp(img, "PTBL", 4)) {
		fprintf(stderr, "pas d'en-tête PTBL : ce n'est pas uefivarstore\n");
		return -1;
	}
	uint32_t n = rd32(img + 0x0c);

	if (n == 0 || n > 8) {
		fprintf(stderr, "PTBL : %u tables, inattendu\n", n);
		return -1;
	}
	for (uint32_t i = 0; i < n; i++) {
		const uint8_t *e = img + 0x10 + 16 * i;
		struct table *t = &tables[nb_tables++];

		t->type = rd32(e);
		t->premier = rd16(e + 4);
		t->nb = rd16(e + 6);
		if ((size_t)(t->premier + t->nb) * BLOC > taille || t->nb < 6) {
			fprintf(stderr, "PTBL : table %u hors de la partition\n", t->type);
			return -1;
		}
		uint32_t moitie = t->nb / 2;

		lire_banc(&t->b[0], (size_t)t->premier * BLOC, moitie - 2);
		lire_banc(&t->b[1], (size_t)(t->premier + moitie) * BLOC, moitie - 2);
	}
	return 0;
}

static int actif(const struct table *t)
{
	if (t->b[0].valide && t->b[1].valide)
		return t->b[0].gen >= t->b[1].gen ? 0 : 1;
	if (t->b[0].valide)
		return 0;
	if (t->b[1].valide)
		return 1;
	return -1;
}

/* parcours des variables ; rappel pour chacune ; renvoie -1 si incohérent */
typedef int (*visiteur)(const uint8_t *guid, const uint8_t *nom, uint16_t nom_len,
			uint8_t *donnees, uint32_t len, void *ctx);

static int parcourir(uint8_t *v, uint32_t len, visiteur f, void *ctx)
{
	uint32_t g = 0x14;

	while (g < len) {
		if (g + 0x14 > len)
			return -1;
		uint32_t gtaille = rd32(v + g);

		if (gtaille < 0x14 || g + gtaille > len)
			return -1;
		const uint8_t *guid = v + g + 4;
		uint32_t r = g + 0x14;

		while (r < g + gtaille) {
			if (r + 0x12 > g + gtaille)
				return -1;
			uint32_t rtaille = rd32(v + r);
			uint32_t dlen = rd32(v + r + 0x0c);
			uint16_t nlen = rd16(v + r + 0x10);
			uint32_t doff = (0x12 + nlen + 3) & ~3u;

			if (rtaille < 0x12 || r + rtaille > g + gtaille ||
			    doff + dlen > rtaille || nlen < 2 || nlen & 1)
				return -1;
			int s = f(guid, v + r + 0x12, nlen, v + r + doff, dlen, ctx);

			if (s)
				return s;
			r += rtaille;
		}
		if (r != g + gtaille)
			return -1;
		g += gtaille;
	}
	return g == len ? 0 : -1;
}

static void afficher_guid(const uint8_t *g)
{
	printf("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", rd32(g), rd16(g + 4),
	       rd16(g + 6), g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

static int lister_var(const uint8_t *guid, const uint8_t *nom, uint16_t nom_len,
		      uint8_t *d, uint32_t len, void *ctx)
{
	(void)ctx;
	printf("      {");
	afficher_guid(guid);
	printf("} ");
	for (uint16_t i = 0; i + 1 < nom_len && nom[i]; i += 2)
		putchar(nom[i] < 0x80 ? nom[i] : '?');
	printf(" (%u o) :", len);
	for (uint32_t i = 0; i < len && i < 16; i++)
		printf(" %02x", d[i]);
	printf("%s\n", len > 16 ? " …" : "");
	return 0;
}

static int est_rtcinfo(const uint8_t *guid, const uint8_t *nom, uint16_t nom_len)
{
	static const char n[] = "RTCInfo";

	if (memcmp(guid, rtc_guid, 16) || nom_len != 2 * sizeof(n))
		return 0;
	for (size_t i = 0; i < sizeof(n); i++)
		if (nom[2 * i] != (uint8_t)n[i] || nom[2 * i + 1])
			return 0;
	return 1;
}

static int trouver_rtc(const uint8_t *guid, const uint8_t *nom, uint16_t nom_len,
		       uint8_t *d, uint32_t len, void *ctx)
{
	if (!est_rtcinfo(guid, nom, nom_len))
		return 0;
	if (len != 12)
		return -2;
	*(uint8_t **)ctx = d;
	return 1;
}

static void liste(void)
{
	for (int i = 0; i < nb_tables; i++) {
		struct table *t = &tables[i];
		int a = actif(t);

		printf("table %u : blocs %u à %u\n", t->type, t->premier, t->premier + t->nb - 1);
		for (int k = 0; k < 2; k++) {
			struct banc *b = &t->b[k];

			printf("  banc %s (octet 0x%zx) : %s", k ? "haut" : "bas", b->debut,
			       b->valide ? "valide" : "absent ou invalide");
			if (b->valide)
				printf(", génération %u, %u o%s", b->gen, b->var2_len,
				       k == a ? "  ← actif" : "");
			printf("\n");
		}
		if (a >= 0) {
			uint8_t *v = img + t->b[a].debut + BLOC;

			if (parcourir(v, t->b[a].var2_len, lister_var, NULL) < 0)
				printf("      (contenu non reconnu)\n");
		}
	}
}

static int charger(const char *chemin)
{
	int fd = open(chemin, O_RDONLY);

	if (fd < 0) {
		fprintf(stderr, "%s : %s\n", chemin, strerror(errno));
		return -1;
	}
	img = malloc(TAILLE_MAX);
	ssize_t n = img ? pread(fd, img, TAILLE_MAX, 0) : -1;

	close(fd);
	if (n <= 0) {
		fprintf(stderr, "lecture de %s impossible\n", chemin);
		return -1;
	}
	taille = n;
	return 0;
}

static int sauvegarder(void)
{
	char nom[64];
	time_t t = time(NULL);
	FILE *f;

	strftime(nom, sizeof(nom), "uefivarstore-%Y%m%d-%H%M%S.img", localtime(&t));
	f = fopen(nom, "wx");
	if (!f || fwrite(img, 1, taille, f) != taille || fflush(f) || fsync(fileno(f))) {
		fprintf(stderr, "sauvegarde %s impossible : rien n'est écrit\n", nom);
		if (f)
			fclose(f);
		return -1;
	}
	fclose(f);
	printf("sauvegarde : %s\n", nom);
	return 0;
}

static int ecrire_bloc(int fd, size_t off, size_t n)
{
	if (pwrite(fd, img + off, n, off) != (ssize_t)n || fsync(fd)) {
		fprintf(stderr, "écriture à 0x%zx : %s\n", off, strerror(errno));
		return -1;
	}
	return 0;
}

static int rtcinfo(const char *chemin, int est_fichier, uint32_t gps)
{
	struct table *t = NULL;
	uint8_t *d = NULL;
	int a = -1;

	/* la table qui contient RTCInfo, dans son banc actif */
	for (int i = 0; i < nb_tables && !d; i++) {
		int k = actif(&tables[i]);

		if (k < 0)
			continue;
		int s = parcourir(img + tables[i].b[k].debut + BLOC, tables[i].b[k].var2_len,
				  trouver_rtc, &d);

		if (s == -2) {
			fprintf(stderr, "RTCInfo n'a pas 12 octets : rien n'est écrit\n");
			return -1;
		}
		if (s == 1) {
			t = &tables[i];
			a = k;
		}
	}
	if (!d) {
		fprintf(stderr, "RTCInfo introuvable : rien n'est écrit\n");
		return -1;
	}

	struct banc *src = &t->b[a];
	int k = !a;
	size_t dst = k ? (size_t)(t->premier + t->nb / 2) * BLOC : (size_t)t->premier * BLOC;
	uint32_t ancien = rd32(d);

	printf("RTCInfo : table %u, banc %s génération %u ; décalage GPS %u → %u\n",
	       t->type, a ? "haut" : "bas", src->gen, ancien, gps);
	if (ancien == gps) {
		printf("déjà à jour, rien à écrire\n");
		return 0;
	}
	if (!est_fichier && sauvegarder())
		return -1;

	/* nouveau banc : copie du banc actif, RTCInfo changé, génération + 1 */
	size_t n = (size_t)(src->nblk + 2) * BLOC;

	memmove(img + dst, img + src->debut, n);
	d = d - img - src->debut + dst + img;
	wr32(d, gps);

	uint8_t *h = img + dst;

	h[6] = k ? 1 : 2;
	wr32(h + 0x10, src->gen + 1);
	wr32(h + 0x14, crc32(h + BLOC, src->var2_len));
	wr32(h + 0x28, crc32(h, 0x28));
	memcpy(img + dst + (1 + src->nblk) * BLOC, h, BLOC);

	int fd = open(chemin, O_WRONLY);

	if (fd < 0) {
		fprintf(stderr, "%s : %s\n", chemin, strerror(errno));
		return -1;
	}
	/* même ordre que DataMgrWrite : en-tête, données utilisées, copie */
	if (ecrire_bloc(fd, dst, BLOC) ||
	    ecrire_bloc(fd, dst + BLOC, (size_t)src->utilises * BLOC) ||
	    ecrire_bloc(fd, dst + (1 + src->nblk) * BLOC, BLOC)) {
		close(fd);
		return -1;
	}
	close(fd);

	/* relecture et contrôle */
	free(img);
	nb_tables = 0;
	if (charger(chemin) || lire_ptbl())
		return -1;
	struct table *t2 = NULL;

	for (int i = 0; i < nb_tables; i++)
		if (tables[i].type == t->type)
			t2 = &tables[i];
	if (!t2 || actif(t2) != k || t2->b[k].gen != src->gen + 1) {
		fprintf(stderr, "contrôle après écriture : le nouveau banc n'est pas le banc actif !\n");
		return -1;
	}
	printf("écrit : banc %s, génération %u ; pris en compte au prochain démarrage\n",
	       k ? "haut" : "bas", t2->b[k].gen);
	return 0;
}

int main(int argc, char **argv)
{
	const char *chemin = "/dev/disk/by-partlabel/uefivarstore";
	int est_fichier = 0, i = 1;

	if (argc > 2 && !strcmp(argv[1], "-f")) {
		chemin = argv[2];
		est_fichier = 1;
		i = 3;
	}
	if (i >= argc) {
		fprintf(stderr, "usage : %s [-f FICHIER] liste | rtcinfo GPS | rtcinfo-unix UNIX | rtcinfo-auto\n",
			argv[0]);
		return 2;
	}
	if (charger(chemin) || lire_ptbl())
		return 1;
	if (!strcmp(argv[i], "liste")) {
		liste();
		return 0;
	}
	if (!strcmp(argv[i], "rtcinfo-auto")) {
		/* rtc-pm8xxx sans décalage : since_epoch = compteur brut du PMIC */
		FILE *f = fopen("/sys/class/rtc/rtc0/since_epoch", "r");
		long long brut;

		if (!f || fscanf(f, "%lld", &brut) != 1) {
			fprintf(stderr, "/sys/class/rtc/rtc0/since_epoch illisible\n");
			return 1;
		}
		fclose(f);
		long long v = (long long)time(NULL) - brut - GPS_EPOCH;

		printf("compteur RTC %lld s, heure %lld : décalage GPS %lld s\n", brut,
		       (long long)time(NULL), v);
		if (v < 0 || v > 0xffffffffLL)
			return 1;
		return rtcinfo(chemin, est_fichier, (uint32_t)v) ? 1 : 0;
	}
	if ((!strcmp(argv[i], "rtcinfo") || !strcmp(argv[i], "rtcinfo-unix")) && i + 1 < argc) {
		long long v = strtoll(argv[i + 1], NULL, 0);

		if (!strcmp(argv[i], "rtcinfo-unix"))
			v -= GPS_EPOCH;
		if (v < 0 || v > 0xffffffffLL) {
			fprintf(stderr, "valeur hors limites\n");
			return 2;
		}
		return rtcinfo(chemin, est_fichier, (uint32_t)v) ? 1 : 0;
	}
	fprintf(stderr, "commande inconnue\n");
	return 2;
}
