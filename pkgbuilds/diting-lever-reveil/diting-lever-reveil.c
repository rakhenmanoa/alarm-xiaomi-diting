/*
 * diting-lever-reveil : lever le téléphone pour rallumer l'écran.
 *
 * Le SLPI du Xiaomi diting fait tourner sns_tilt_to_wake (algorithme
 * Qualcomm sur l'accéléromètre) ; libssc (patch 0001 du paquet libssc)
 * l'expose comme SSCSensorTiltToWake. Les événements sont livrés avec
 * réveil : ils sortent aussi le téléphone du suspend. À chaque geste,
 * on rallume l'affichage avec KWin (kscreen-doctor), sauf :
 *  - capteur de proximité couvert (poche, face contre la table) : l'état
 *    est lu dans iio-sensor-proxy (propriété en cache, aucun délai) ;
 *  - appel actif : l'écran est géré par diting-proximite-appel (porter le
 *    téléphone à l'oreille est aussi un geste « lever ») ;
 *  - moins de DELAI_GESTE après le dernier rallumage : en main, le SLPI
 *    renvoie le geste plusieurs fois par seconde, et ces rafales de
 *    « dpms on » se chevauchaient dans KWin (Atomic commit failed: EBUSY).
 *
 * Service utilisateur de la session Plasma (diting-lever-reveil.service).
 *
 * Le capteur est ouvert en asynchrone : l'ouverture ne se termine jamais
 * (le SLPI ne répond pas par une mise à jour de configuration pour ce
 * capteur, et son premier événement n'arrive qu'au premier geste), et les
 * fonctions *_sync de libssc attendent en faisant tourner la boucle GLib
 * sans bloquer, à 100 % d'un cœur. Pour la même raison, pas de fermeture
 * explicite : le SLPI libère le client quand la socket QRTR se ferme.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <libssc.h>

/* Délai minimal entre deux rallumages de l'écran */
#define DELAI_GESTE (3 * G_USEC_PER_SEC)

static GMainLoop *loop;
static gint64 dernier_rallumage;	/* g_get_monotonic_time(), 0 = jamais */
static GDBusProxy *capteurs;	/* net.hadess.SensorProxy, NULL si absent */

/*****************************************************************************/
/* Proximité (iio-sensor-proxy) */

static void
reserver_proximite (void)
{
	g_autofree gchar *proprio = g_dbus_proxy_get_name_owner (capteurs);

	if (!proprio)
		return;
	/* La réservation suit l'iio-sensor-proxy en cours : à refaire s'il redémarre */
	g_dbus_proxy_call (capteurs, "ClaimProximity", NULL, G_DBUS_CALL_FLAGS_NONE,
			   -1, NULL, NULL, NULL);
}

static void
proprio_change (GObject *obj, GParamSpec *pspec, gpointer user_data)
{
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
	reserver_proximite ();
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
geste_detecte (SSCSensorTiltToWake *capteur, gfloat valeur, guint msg_id, gpointer user_data)
{
	gchar *argv[] = { "kscreen-doctor", "--dpms", "on", NULL };
	g_autoptr (GError) err = NULL;
	gint64 maintenant = g_get_monotonic_time ();

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
	if (!g_spawn_async (NULL, argv, NULL,
			    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			    NULL, NULL, NULL, &err))
		g_warning ("kscreen-doctor : %s", err->message);
}

static void
ouverture_terminee (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr (GError) err = NULL;

	if (!ssc_sensor_tilt_to_wake_open_finish (SSC_SENSOR_TILT_TO_WAKE (source), res, &err))
		g_warning ("ouverture du capteur : %s", err ? err->message : "?");
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
	SSCSensorTiltToWake *capteur;

	g_setenv ("QT_QPA_PLATFORM", "wayland", TRUE);

	init_proximite ();

	capteur = ssc_sensor_tilt_to_wake_new_sync (NULL, &err);
	if (!capteur) {
		g_printerr ("capteur tilt to wake indisponible : %s\n", err ? err->message : "?");
		return EXIT_FAILURE;
	}
	g_signal_connect (capteur, "detected", G_CALLBACK (geste_detecte), NULL);

	ssc_sensor_tilt_to_wake_open (capteur, NULL, ouverture_terminee, NULL);
	g_message ("en attente du geste « lever pour réveiller »");

	loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add (SIGTERM, arreter, NULL);
	g_unix_signal_add (SIGINT, arreter, NULL);
	g_main_loop_run (loop);

	if (capteurs) {
		g_dbus_proxy_call_sync (capteurs, "ReleaseProximity", NULL, G_DBUS_CALL_FLAGS_NONE,
					1000, NULL, NULL);
		g_object_unref (capteurs);
	}
	g_object_unref (capteur);
	g_main_loop_unref (loop);
	return EXIT_SUCCESS;
}
