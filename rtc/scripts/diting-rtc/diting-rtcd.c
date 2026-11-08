// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * diting-rtcd : heure système tenue par le RTC du PMIC (PMK8350) du diting.
 *
 * Le RTC de rtc-pm8xxx est en lecture seule sur les téléphones Qualcomm :
 * le compteur du PMIC tourne sans arrêt (même éteint) mais ne peut pas être
 * mis à l'heure. Android garde donc un décalage « heure réelle - compteur »
 * dans son time_daemon. Ce démon fait la même chose :
 *
 *   - au démarrage : heure = compteur RTC + décalage enregistré ;
 *   - à chaque changement d'heure (NTP, réglage manuel) : nouveau décalage ;
 *   - toutes les heures si l'horloge est synchronisée (dérive du RTC) ;
 *   - à l'arrêt.
 *
 * Le PMIC n'est jamais écrit : l'heure d'Android reste intacte.
 *
 * Fichier d'état (texte) : « <décalage> <compteur au moment de l'écriture> ».
 * Si le compteur est revenu en arrière (batterie vidée, RTC remis à zéro),
 * le décalage n'est plus valable et n'est pas appliqué.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/timex.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define RTC_DEV		"/dev/rtc0"
#define HCTOSYS_ATTR	"/sys/class/rtc/rtc0/hctosys"
#define PERIODE		3600	/* s, sauvegarde de la dérive */

static const char *etat_dir = "/var/lib/diting-rtc";
static char etat[256], etat_tmp[256];

static int rtc_fd = -1;
static int64_t decalage;	/* heure réelle - compteur RTC, en s */
static int decalage_connu;	/* restauré au démarrage ou heure réglée */
static volatile sig_atomic_t fin;

static void msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static int lire_rtc(int64_t *s)
{
	struct rtc_time t;
	struct tm tm = { 0 };

	if (ioctl(rtc_fd, RTC_RD_TIME, &t) < 0)
		return -errno;
	tm.tm_sec = t.tm_sec;
	tm.tm_min = t.tm_min;
	tm.tm_hour = t.tm_hour;
	tm.tm_mday = t.tm_mday;
	tm.tm_mon = t.tm_mon;
	tm.tm_year = t.tm_year;
	*s = (int64_t)timegm(&tm);
	return 0;
}

static int64_t maintenant(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec;
}

static int synchronisee(void)
{
	struct timex tx = { 0 };

	return adjtimex(&tx) != TIME_ERROR && !(tx.status & STA_UNSYNC);
}

static int lire_etat(int64_t *dec, int64_t *brut)
{
	long long d, b;
	FILE *f = fopen(etat, "r");
	int n;

	if (!f)
		return -1;
	n = fscanf(f, "%lld %lld", &d, &b);
	fclose(f);
	if (n != 2)
		return -1;
	*dec = d;
	*brut = b;
	return 0;
}

static void ecrire_etat(int64_t dec, int64_t brut)
{
	FILE *f = fopen(etat_tmp, "w");

	if (!f) {
		msg("écriture de %s impossible : %s", etat_tmp, strerror(errno));
		return;
	}
	fprintf(f, "%lld %lld\n", (long long)dec, (long long)brut);
	fflush(f);
	fsync(fileno(f));
	fclose(f);
	if (rename(etat_tmp, etat) < 0)
		msg("rename %s : %s", etat, strerror(errno));
}

/* enregistre le décalage courant si l'écart dépasse « seuil » secondes */
static void sauver(int64_t seuil, const char *raison)
{
	int64_t brut, nouveau, ecart;

	if (lire_rtc(&brut) < 0)
		return;
	nouveau = maintenant() - brut;
	ecart = llabs(nouveau - decalage);
	if (decalage_connu && ecart < seuil)
		return;
	ecrire_etat(nouveau, brut);
	msg("%s : décalage %lld s (écart %lld s)", raison, (long long)nouveau,
	    (long long)(decalage_connu ? ecart : 0));
	decalage = nouveau;
	decalage_connu = 1;
}

static int appliquer(int64_t brut)
{
	struct timespec ts = { .tv_sec = brut + decalage };

	if (clock_settime(CLOCK_REALTIME, &ts) < 0) {
		msg("clock_settime : %s", strerror(errno));
		return -1;
	}
	return 0;
}

/* attend que le noyau ait fini hctosys (sinon il écraserait notre heure) */
static void attendre_hctosys(void)
{
	for (int i = 0; i < 30; i++) {
		char c = '0';
		int fd = open(HCTOSYS_ATTR, O_RDONLY);

		if (fd >= 0) {
			if (read(fd, &c, 1) != 1)
				c = '0';
			close(fd);
		}
		if (c == '1')
			return;
		usleep(100 * 1000);
	}
}

static void restaurer(void)
{
	int64_t dec, brut_sauve, brut;
	char t[64];
	time_t h;

	if (lire_rtc(&brut) < 0) {
		msg("lecture du RTC impossible");
		return;
	}
	if (lire_etat(&dec, &brut_sauve) < 0) {
		msg("pas de décalage enregistré : heure laissée à NTP / réglage manuel");
		return;
	}
	if (brut + 1 < brut_sauve) {
		msg("compteur RTC revenu en arrière (%lld < %lld) : RTC remis à zéro, décalage ignoré",
		    (long long)brut, (long long)brut_sauve);
		return;
	}
	decalage = dec;
	decalage_connu = 1;
	if (appliquer(brut) == 0) {
		h = (time_t)(brut + dec);
		strftime(t, sizeof(t), "%F %T UTC", gmtime(&h));
		msg("heure restaurée depuis le RTC : %s", t);
	}
}

static void notifier_systemd(void)
{
	const char *s = getenv("NOTIFY_SOCKET");
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	socklen_t len;
	int fd;

	if (!s || !*s || strlen(s) >= sizeof(sa.sun_path))
		return;
	strcpy(sa.sun_path, s);
	if (sa.sun_path[0] == '@')
		sa.sun_path[0] = '\0';
	len = offsetof(struct sockaddr_un, sun_path) + strlen(s);
	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return;
	sendto(fd, "READY=1", 7, 0, (struct sockaddr *)&sa, len);
	close(fd);
}

static void sur_signal(int sig)
{
	(void)sig;
	fin = 1;
}

static int armer(int tfd)
{
	struct itimerspec its = { 0 };

	its.it_value.tv_sec = maintenant() + PERIODE;
	return timerfd_settime(tfd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET,
			       &its, NULL);
}

int main(int argc, char **argv)
{
	struct sigaction sa = { .sa_handler = sur_signal };
	int tfd;

	if (argc > 1)
		etat_dir = argv[1];
	snprintf(etat, sizeof(etat), "%s/offset", etat_dir);
	snprintf(etat_tmp, sizeof(etat_tmp), "%s/offset.tmp", etat_dir);

	for (int i = 0; i < 100 && rtc_fd < 0; i++) {
		rtc_fd = open(RTC_DEV, O_RDONLY | O_CLOEXEC);
		if (rtc_fd < 0)
			usleep(100 * 1000);
	}
	if (rtc_fd < 0) {
		msg("%s absent : %s", RTC_DEV, strerror(errno));
		notifier_systemd();
		return 1;
	}

	attendre_hctosys();
	restaurer();
	notifier_systemd();

	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	tfd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC);
	if (tfd < 0 || armer(tfd) < 0) {
		msg("timerfd : %s", strerror(errno));
		return 1;
	}

	while (!fin) {
		uint64_t n;
		ssize_t r = read(tfd, &n, sizeof(n));

		if (r < 0 && errno == ECANCELED) {
			/* l'heure a été changée (NTP, réglage manuel, hctosys) */
			int64_t brut;

			if (lire_rtc(&brut) == 0 && decalage_connu &&
			    llabs(decalage) > 2 && llabs(maintenant() - brut) <= 2) {
				/* heure ramenée au compteur brut : hctosys tardif */
				msg("heure remise au compteur brut du RTC : décalage réappliqué");
				appliquer(brut);
			} else {
				sauver(1, "heure changée");
			}
		} else if (r == sizeof(n)) {
			if (synchronisee())
				sauver(2, "dérive (NTP)");
		} else if (r < 0 && errno != EINTR) {
			msg("read timerfd : %s", strerror(errno));
			break;
		}
		armer(tfd);
	}

	if (decalage_connu || synchronisee())
		sauver(1, "arrêt");
	return 0;
}
