/*
 * diting-lever-reveil : lever le téléphone pour rallumer l'écran.
 *
 * Le SLPI du Xiaomi diting fait tourner sns_tilt_to_wake (algorithme
 * Qualcomm sur l'accéléromètre) ; libssc (patch 0001 du paquet libssc)
 * l'expose comme SSCSensorTiltToWake. Les événements sont livrés avec
 * réveil : ils sortent aussi le téléphone du suspend.
 *
 * Écran éteint seulement : l'état de l'écran est suivi en direct avec le
 * protocole Wayland DPMS de KWin (org_kde_kwin_dpms, celui qu'utilise
 * kscreen-doctor --dpms). Quand l'écran s'éteint, on ouvre le capteur du
 * geste et on réserve la proximité dans iio-sensor-proxy ; quand il se
 * rallume, on ferme l'un et on libère l'autre. Écran allumé, le SLPI
 * n'échantillonne donc ni le geste ni la proximité pour ce démon (moins
 * d'interruptions SMP2P « handover », moins de batterie).
 *
 * À chaque geste, on rallume l'écran (requête DPMS « On » à KWin), sauf :
 *  - écran déjà allumé (événement arrivé pendant la fermeture du capteur) ;
 *  - capteur de proximité couvert (poche, face contre la table) : l'état
 *    est lu dans iio-sensor-proxy (propriété en cache, aucun délai) ;
 *  - appel actif : l'écran est géré par diting-proximite-appel (porter le
 *    téléphone à l'oreille est aussi un geste « lever ») ;
 *  - moins de DELAI_GESTE après le dernier rallumage : en main, le SLPI
 *    renvoie le geste plusieurs fois par seconde, et ces rafales de
 *    « dpms on » se chevauchaient dans KWin (Atomic commit failed: EBUSY).
 *
 * Service utilisateur de la session Plasma (diting-lever-reveil.service),
 * qui lui transmet WAYLAND_DISPLAY.
 *
 * L'ouverture du capteur ne se termine jamais (le SLPI ne répond pas par
 * une mise à jour de configuration pour ce capteur, et son premier
 * événement n'arrive qu'au premier geste) : tout passe par les fonctions
 * asynchrones de libssc, jamais par les *_sync, qui attendraient en
 * faisant tourner la boucle GLib à 100 % d'un cœur.
 *
 * DITING_LEVER_GARDER_OUVERT=1 (dans l'environnement du service) garde le
 * capteur et la proximité ouverts en permanence, comme avant la 0.3.0 ;
 * seule la vérification « écran déjà allumé » reste.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <libssc.h>
#include <wayland-client.h>
#include "dpms-client-protocol.h"

/* Délai minimal entre deux rallumages de l'écran */
#define DELAI_GESTE (3 * G_USEC_PER_SEC)

static GMainLoop *loop;
static int code_sortie = EXIT_SUCCESS;
static gint64 dernier_rallumage;	/* g_get_monotonic_time(), 0 = jamais */
static gboolean garder_ouvert;		/* DITING_LEVER_GARDER_OUVERT=1 */

static GDBusProxy *capteurs;		/* net.hadess.SensorProxy, NULL si absent */
static gboolean proximite_reservee;

static SSCSensorTiltToWake *capteur;
static gboolean capteur_ouvert;

/*****************************************************************************/
/* Proximité (iio-sensor-proxy) */

static void
reserver_proximite (void)
{
	g_autofree gchar *proprio = NULL;

	if (!capteurs || proximite_reservee)
		return;
	proprio = g_dbus_proxy_get_name_owner (capteurs);
	if (!proprio)
		return;
	g_dbus_proxy_call (capteurs, "ClaimProximity", NULL, G_DBUS_CALL_FLAGS_NONE,
			   -1, NULL, NULL, NULL);
	proximite_reservee = TRUE;
}

static void
liberer_proximite (void)
{
	if (!capteurs || !proximite_reservee)
		return;
	g_dbus_proxy_call (capteurs, "ReleaseProximity", NULL, G_DBUS_CALL_FLAGS_NONE,
			   -1, NULL, NULL, NULL);
	proximite_reservee = FALSE;
}

static gboolean ecran_allume = TRUE;

static void
proprio_change (GObject *obj, GParamSpec *pspec, gpointer user_data)
{
	/* La réservation suit l'iio-sensor-proxy en cours : à refaire s'il redémarre */
	proximite_reservee = FALSE;
	if (garder_ouvert || !ecran_allume)
		reserver_proximite ();
}

static void
init_proximite (void)
{
	g_autoptr (GError) err = NULL;

	capteurs = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
						  "net.hadess.SensorProxy", "/net/hadess/SensorProxy",
						  "net.hadess.SensorProxy", NULL, &err);
	if (!capteurs) {
		g_warning ("iio-sensor-proxy injoignable, pas de garde de proximité : %s", err->message);
		return;
	}
	g_signal_connect (capteurs, "notify::g-name-owner", G_CALLBACK (proprio_change), NULL);
}

static gboolean
proximite_couverte (void)
{
	g_autoptr (GVariant) a = NULL;
	g_autoptr (GVariant) proche = NULL;

	if (!capteurs)
		return FALSE;
	a = g_dbus_proxy_get_cached_property (capteurs, "HasProximity");
	if (!a || !g_variant_get_boolean (a))
		return FALSE;
	proche = g_dbus_proxy_get_cached_property (capteurs, "ProximityNear");
	return proche && g_variant_get_boolean (proche);
}

/*****************************************************************************/
/* Capteur du geste (SLPI) */

static void
ouverture_terminee (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr (GError) err = NULL;

	if (!ssc_sensor_tilt_to_wake_open_finish (SSC_SENSOR_TILT_TO_WAKE (source), res, &err))
		g_warning ("ouverture du capteur : %s", err ? err->message : "?");
}

static void
fermeture_terminee (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr (GError) err = NULL;

	if (!ssc_sensor_tilt_to_wake_close_finish (SSC_SENSOR_TILT_TO_WAKE (source), res, &err))
		g_warning ("fermeture du capteur : %s", err ? err->message : "?");
}

static void
ouvrir_capteur (void)
{
	if (capteur_ouvert)
		return;
	ssc_sensor_tilt_to_wake_open (capteur, NULL, ouverture_terminee, NULL);
	capteur_ouvert = TRUE;
}

static void
fermer_capteur (void)
{
	if (!capteur_ouvert)
		return;
	ssc_sensor_tilt_to_wake_close (capteur, NULL, fermeture_terminee, NULL);
	capteur_ouvert = FALSE;
}

/*****************************************************************************/
/* État de l'écran (DPMS de KWin) */

typedef struct {
	guint32 nom;			/* nom global wl_registry */
	struct wl_output *sortie;
	struct org_kde_kwin_dpms *dpms;
	guint32 mode;
} Ecran;

static struct wl_display *affichage;
static struct wl_registry *registre;
static struct org_kde_kwin_dpms_manager *gestionnaire_dpms;
static GPtrArray *ecrans;		/* Ecran* */
static gboolean etat_connu;

static void
etat_ecran_change (void)
{
	gboolean allume = FALSE, suivi = FALSE;
	guint i;

	for (i = 0; i < ecrans->len; i++) {
		Ecran *e = g_ptr_array_index (ecrans, i);

		if (!e->dpms)
			continue;
		suivi = TRUE;
		if (e->mode == ORG_KDE_KWIN_DPMS_MODE_ON)
			allume = TRUE;
	}
	if (!suivi)
		return;			/* aucune sortie connue : on ne change rien */
	if (etat_connu && allume == ecran_allume)
		return;
	etat_connu = TRUE;
	ecran_allume = allume;

	if (garder_ouvert) {
		g_message ("écran %s", allume ? "allumé" : "éteint");
		return;
	}
	if (allume) {
		g_message ("écran allumé : capteur du geste fermé");
		fermer_capteur ();
		liberer_proximite ();
	} else {
		g_message ("écran éteint : capteur du geste ouvert");
		reserver_proximite ();
		ouvrir_capteur ();
	}
}

static void
dpms_supporte (void *data, struct org_kde_kwin_dpms *dpms, uint32_t supporte)
{
	/* sans DPMS, KWin annonce le mode On : rien à faire de plus */
}

static void
dpms_mode (void *data, struct org_kde_kwin_dpms *dpms, uint32_t mode)
{
	((Ecran *) data)->mode = mode;
}

static void
dpms_fait (void *data, struct org_kde_kwin_dpms *dpms)
{
	etat_ecran_change ();
}

static const struct org_kde_kwin_dpms_listener ecouteur_dpms = {
	.supported = dpms_supporte,
	.mode = dpms_mode,
	.done = dpms_fait,
};

static void
suivre_ecran (Ecran *e)
{
	if (!gestionnaire_dpms || e->dpms)
		return;
	e->dpms = org_kde_kwin_dpms_manager_get (gestionnaire_dpms, e->sortie);
	org_kde_kwin_dpms_add_listener (e->dpms, &ecouteur_dpms, e);
}

static void
liberer_ecran (gpointer data)
{
	Ecran *e = data;

	if (e->dpms)
		org_kde_kwin_dpms_release (e->dpms);
	wl_output_destroy (e->sortie);
	g_free (e);
}

static void
global_ajoute (void *data, struct wl_registry *reg, uint32_t nom,
	       const char *interface, uint32_t version)
{
	guint i;

	if (strcmp (interface, wl_output_interface.name) == 0) {
		Ecran *e = g_new0 (Ecran, 1);

		e->nom = nom;
		e->mode = ORG_KDE_KWIN_DPMS_MODE_ON;
		e->sortie = wl_registry_bind (reg, nom, &wl_output_interface, 1);
		g_ptr_array_add (ecrans, e);
		suivre_ecran (e);
	} else if (strcmp (interface, org_kde_kwin_dpms_manager_interface.name) == 0 &&
		   !gestionnaire_dpms) {
		gestionnaire_dpms = wl_registry_bind (reg, nom, &org_kde_kwin_dpms_manager_interface, 1);
		for (i = 0; i < ecrans->len; i++)
			suivre_ecran (g_ptr_array_index (ecrans, i));
	}
}

static void
global_retire (void *data, struct wl_registry *reg, uint32_t nom)
{
	guint i;

	for (i = 0; i < ecrans->len; i++) {
		Ecran *e = g_ptr_array_index (ecrans, i);

		if (e->nom == nom) {
			g_ptr_array_remove_index_fast (ecrans, i);
			etat_ecran_change ();
			return;
		}
	}
}

static const struct wl_registry_listener ecouteur_registre = {
	.global = global_ajoute,
	.global_remove = global_retire,
};

static void
rallumer_ecran (void)
{
	guint i;

	for (i = 0; i < ecrans->len; i++) {
		Ecran *e = g_ptr_array_index (ecrans, i);

		if (e->dpms)
			org_kde_kwin_dpms_set (e->dpms, ORG_KDE_KWIN_DPMS_MODE_ON);
	}
	wl_display_flush (affichage);
}

static gboolean
wayland_lisible (gint fd, GIOCondition cond, gpointer user_data)
{
	if ((cond & (G_IO_ERR | G_IO_HUP)) || wl_display_dispatch (affichage) < 0) {
		g_warning ("connexion à KWin perdue");
		code_sortie = EXIT_FAILURE;
		g_main_loop_quit (loop);
		return G_SOURCE_REMOVE;
	}
	wl_display_flush (affichage);
	return G_SOURCE_CONTINUE;
}

static gboolean
init_wayland (void)
{
	affichage = wl_display_connect (NULL);
	if (!affichage) {
		g_printerr ("connexion Wayland impossible (WAYLAND_DISPLAY ?)\n");
		return FALSE;
	}
	ecrans = g_ptr_array_new_with_free_func (liberer_ecran);
	registre = wl_display_get_registry (affichage);
	wl_registry_add_listener (registre, &ecouteur_registre, NULL);

	/* 1er aller-retour : les globaux ; 2e : l'état DPMS initial de chaque sortie */
	if (wl_display_roundtrip (affichage) < 0 || wl_display_roundtrip (affichage) < 0) {
		g_printerr ("échange Wayland initial impossible\n");
		return FALSE;
	}
	if (!gestionnaire_dpms) {
		g_printerr ("KWin n'expose pas org_kde_kwin_dpms_manager\n");
		return FALSE;
	}
	g_unix_fd_add (wl_display_get_fd (affichage), G_IO_IN | G_IO_ERR | G_IO_HUP,
		       wayland_lisible, NULL);
	wl_display_flush (affichage);
	return TRUE;
}

/*****************************************************************************/

static gboolean
appel_actif (void)
{
	gchar *argv[] = { "mmcli", "-m", "any", "--voice-list-calls", NULL };
	g_autofree gchar *out = NULL;

	if (!g_spawn_sync (NULL, argv, NULL,
			   G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
			   NULL, NULL, &out, NULL, NULL, NULL))
		return FALSE;
	return out && strstr (out, "(active)") != NULL;
}

static void
geste_detecte (SSCSensorTiltToWake *source, gfloat valeur, guint msg_id, gpointer user_data)
{
	gint64 maintenant = g_get_monotonic_time ();

	if (ecran_allume) {
		g_debug ("geste %u écran allumé : ignoré", msg_id);
		return;
	}
	if (dernier_rallumage && maintenant - dernier_rallumage < DELAI_GESTE) {
		g_debug ("geste %u moins de %d s après le dernier rallumage : ignoré",
			 msg_id, (int) (DELAI_GESTE / G_USEC_PER_SEC));
		return;
	}
	if (proximite_couverte ()) {
		g_message ("geste avec le capteur de proximité couvert : ignoré");
		return;
	}
	if (appel_actif ()) {
		g_message ("geste pendant un appel : ignoré");
		return;
	}

	g_message ("geste détecté (message %u) : écran rallumé", msg_id);
	dernier_rallumage = maintenant;
	rallumer_ecran ();
}

static gboolean
arreter (gpointer user_data)
{
	g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

int
main (void)
{
	g_autoptr (GError) err = NULL;
	const gchar *env = g_getenv ("DITING_LEVER_GARDER_OUVERT");

	garder_ouvert = env && g_strcmp0 (env, "0") != 0 && *env;
	loop = g_main_loop_new (NULL, FALSE);

	init_proximite ();

	capteur = ssc_sensor_tilt_to_wake_new_sync (NULL, &err);
	if (!capteur) {
		g_printerr ("capteur tilt to wake indisponible : %s\n", err ? err->message : "?");
		return EXIT_FAILURE;
	}
	g_signal_connect (capteur, "detected", G_CALLBACK (geste_detecte), NULL);

	if (garder_ouvert) {
		reserver_proximite ();
		ouvrir_capteur ();
	}

	/* Ouvre le capteur tout de suite si l'écran est déjà éteint */
	if (!init_wayland ())
		return EXIT_FAILURE;

	g_message ("en attente du geste « lever pour réveiller »%s",
		   garder_ouvert ? " (capteur ouvert en permanence)" : " (écran éteint seulement)");

	g_unix_signal_add (SIGTERM, arreter, NULL);
	g_unix_signal_add (SIGINT, arreter, NULL);
	g_main_loop_run (loop);

	/* Le SLPI libère de toute façon le client quand la socket QRTR se ferme */
	if (capteurs) {
		if (proximite_reservee)
			g_dbus_proxy_call_sync (capteurs, "ReleaseProximity", NULL,
						G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
		g_object_unref (capteurs);
	}
	g_ptr_array_unref (ecrans);
	if (gestionnaire_dpms)
		org_kde_kwin_dpms_manager_destroy (gestionnaire_dpms);
	wl_registry_destroy (registre);
	wl_display_disconnect (affichage);
	g_object_unref (capteur);
	g_main_loop_unref (loop);
	return code_sortie;
}
