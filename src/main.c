/**
 * @file main.c
 * Daemon entry point
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>
 */
#include "internal.h"

gboolean apteryx_netconf_debug = FALSE;
gboolean apteryx_netconf_verbose = FALSE;
static gchar *models_path = "./";
static gchar *unix_path = "/tmp/apteryx-netconf";
static gchar *cp_cmd = NULL;
static gchar *rm_cmd = NULL;
GMainLoop *g_loop = NULL;

static gboolean
termination_handler (gpointer arg1)
{
    GMainLoop *loop = (GMainLoop *) arg1;
    g_main_loop_quit (loop);
    return FALSE;
}

static GOptionEntry entries[] = {
    {"debug", 'd', 0, G_OPTION_ARG_NONE, &apteryx_netconf_debug, "Debug", NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &apteryx_netconf_verbose, "Verbose", NULL},
    {"models", 'm', 0, G_OPTION_ARG_STRING, &models_path, "Path to yang models(defaults to \"./\")", NULL},
    {"unix", 'u', 0, G_OPTION_ARG_STRING, &unix_path, "Listen on unix socket (defaults to /tmp/apteryx-netconf)", NULL},
    {"copy", 'c', 0, G_OPTION_ARG_STRING, &cp_cmd, "BASH command to run to copy running->startup", NULL},
    {"remove", 'r', 0, G_OPTION_ARG_STRING, &rm_cmd, "BASH command to run to remove startup config", NULL},
    {NULL}
};

int
main (int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;

    /* Parse options */
    context = g_option_context_new ("- Netconf access to Apteryx");
    g_option_context_add_main_entries (context, entries, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        g_print ("%s", g_option_context_get_help (context, FALSE, NULL));
        g_error ("%s\n", error->message);
    }

    /* Initialization */
    apteryx_init (apteryx_netconf_verbose);
    if (!netconf_init (models_path, 830, unix_path, cp_cmd, rm_cmd))
    {
        return EXIT_FAILURE;
    }

    /* Main Loop */
    g_loop = g_main_loop_new (NULL, FALSE);
    g_unix_signal_add (SIGINT, termination_handler, g_loop);
    g_unix_signal_add (SIGTERM, termination_handler, g_loop);
    signal (SIGPIPE, SIG_IGN);
    g_main_loop_run (g_loop);

    /* Shutdown */
    netconf_shutdown ();
    apteryx_shutdown ();

    return EXIT_SUCCESS;
}
