/*
 * diting-lever-reveil : lever le téléphone pour rallumer l'écran.
 *
 * Le SLPI du Xiaomi diting fait tourner sns_tilt_to_wake (algorithme
 * Qualcomm sur l'accéléromètre) ; libssc (patch 0001 du paquet libssc)
 * l'expose comme SSCSensorTiltToWake. Les événements sont livrés avec
 * réveil : ils sortent aussi le téléphone du suspend. À chaque geste,
 * on rallume l'affichage avec KWin (kscreen-doctor), sauf pendant un appel
 * actif, où l'écran est géré par diting-proximite-appel (porter le
 * téléphone à l'oreille est aussi un geste « lever »).
 *
 * Service utilisateur de la session Plasma (diting-lever-reveil.service).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-unix.h>
#include <libssc.h>

static GMainLoop *loop;

static gboolean
appel_actif (void)
{
	gchar *argv[] = { "mmcli", "-m", "any", "--voice-list-calls", NULL };
	g_autofree gchar *out = NULL;
	gboolean actif;

	if (!g_spawn_sync (NULL, argv, NULL,
			   G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
			   NULL, NULL, &out, NULL, NULL, NULL))
		return FALSE;
	actif = out && strstr (out, "(active)") != NULL;
	return actif;
}

static void
geste_detecte (SSCSensorTiltToWake *capteur, gfloat valeur, guint msg_id, gpointer user_data)
{
	gchar *argv[] = { "kscreen-doctor", "--dpms", "on", NULL };
	g_autoptr (GError) err = NULL;

	if (appel_actif ()) {
		g_message ("geste pendant un appel : ignoré");
		return;
	}

	g_message ("geste détecté (message %u) : écran rallumé", msg_id);
	if (!g_spawn_async (NULL, argv, NULL,
			    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			    NULL, NULL, NULL, &err))
		g_warning ("kscreen-doctor : %s", err->message);
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

	capteur = ssc_sensor_tilt_to_wake_new_sync (NULL, &err);
	if (!capteur) {
		g_printerr ("capteur tilt to wake indisponible : %s\n", err ? err->message : "?");
		return EXIT_FAILURE;
	}
	g_signal_connect (capteur, "detected", G_CALLBACK (geste_detecte), NULL);

	if (!ssc_sensor_tilt_to_wake_open_sync (capteur, NULL, &err)) {
		g_printerr ("ouverture du capteur impossible : %s\n", err ? err->message : "?");
		return EXIT_FAILURE;
	}
	g_message ("en attente du geste « lever pour réveiller »");

	loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add (SIGTERM, arreter, NULL);
	g_unix_signal_add (SIGINT, arreter, NULL);
	g_main_loop_run (loop);

	g_clear_error (&err);
	if (!ssc_sensor_tilt_to_wake_close_sync (capteur, NULL, &err))
		g_warning ("fermeture du capteur : %s", err ? err->message : "?");
	g_object_unref (capteur);
	g_main_loop_unref (loop);
	return EXIT_SUCCESS;
}
