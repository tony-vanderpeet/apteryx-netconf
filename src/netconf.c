/**
 * @file netconf.c
 * libnetconf2 to Apteryx glue
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
#include <libyang/libyang.h>
#include <nc_server.h>

static GThread *g_thread = NULL;
static struct ly_ctx *g_ctx = NULL;
static const gchar *cp_cmd = NULL;
static const gchar *rm_cmd = NULL;

/* Catch libnetconf logging */
static void
log_cb (NC_VERB_LEVEL level, const char *msg)
{
    switch (level)
    {
    case NC_VERB_DEBUG:
        VERBOSE ("LIBNETCONF: %s\n", msg);
        break;
    case NC_VERB_VERBOSE:
        DEBUG ("LIBNETCONF: %s\n", msg);
        break;
    case NC_VERB_ERROR:
    case NC_VERB_WARNING:
    default:
        ERROR ("LIBNETCONF: %s\n", msg);
        break;
    }
}

/* Convert a netconf xpath to Apteryx tree */
static GNode *
xpath_to_tree (const char *xpath)
{
    GNode *node;
    GNode *leaf;

    //TODO support real xpaths
    //TODO when to wildcard path (e.g. '*')
    node = g_node_new (g_strdup ("/"));
    leaf = apteryx_path_to_node (node, xpath, NULL);
    if (leaf == NULL)
    {
        g_node_destroy (node);
        node = NULL;
    }

    return node;
}

/* Convert a libnetconf XML tree to Apteryx tree */
static GNode *
xml_to_tree (struct lyxml_elem *xml, int depth)
{
    GNode *node;
    char *name;

    /* Parse the node name */
    if (depth == 0)
        name = g_strdup_printf ("/%s", xml->name);
    else
        name = g_strdup (xml->name);

    /* Leaf */
    if (xml->content && xml->content[0] != 0)
    {
        VERBOSE ("%*s%s = %s\n", depth * 2, " ", name, xml->content);
        node = APTERYX_NODE (NULL, name);
        APTERYX_NODE (node, g_strdup (xml->content));
        return node;
    }

    /* Create a node */
    VERBOSE ("%*s%s\n", depth * 2, " ", name);
    node = APTERYX_NODE (NULL, name);

    /* Process children */
    for (struct lyxml_elem * child = xml->child; child; child = child->next)
    {
        GNode *cn = xml_to_tree (child, depth + 1);
        if (cn)
            g_node_append (node, cn);
    }

    return node;
}

/* Convert an Apteryx tree to libnetconf XML tree */
struct lyxml_elem *
tree_to_xml (GNode * node, int depth)
{
    struct lyxml_elem *xml;
    char *name;

    /* Create the node */
    if (depth == 0 && strlen (APTERYX_NAME (node)) == 1)
        return tree_to_xml (node->children, 0);
    else if (depth == 0 && APTERYX_NAME (node)[0] == '/')
        name = g_strdup (APTERYX_NAME (node) + 1);
    else
        name = g_strdup (APTERYX_NAME (node));
    xml = calloc (1, sizeof *xml);
    xml->name = lydict_insert (g_ctx, name, 0);
    xml->prev = xml;

    VERBOSE ("%*s%s\n", depth * 2, " ", name);

    /* Store values */
    if (APTERYX_HAS_VALUE (node))
    {
        xml->content = lydict_insert (g_ctx, g_strdup (APTERYX_VALUE (node)), 0);
        return xml;
    }

    /* Process children */
    for (GNode * child = node->children; child; child = child->next)
    {
        struct lyxml_elem *cn = tree_to_xml (child, depth + 1);
        if (cn)
        {
            /* Add child to parent */
            cn->parent = xml;
            if (xml->child)
            {
                struct lyxml_elem *e = xml->child;
                cn->prev = e->prev;
                cn->next = NULL;
                cn->prev->next = cn;
                e->prev = cn;
            }
            else
            {
                xml->child = cn;
                cn->prev = cn;
                cn->next = NULL;
            }
        }
    }

    return xml;
}

/* Find an attribute */
static const char *
get_attr (struct lyd_node *node, const char *name)
{
    for (struct lyd_attr * attr = node->attr; attr; attr = attr->next)
    {
        if (g_strcmp0 (attr->name, name) == 0)
        {
            return attr->value_str;
        }
    }
    return NULL;
}

/* op_get */
static struct nc_server_reply *
op_get (struct lyd_node *rpc, struct nc_session *ncs)
{
    NC_WD_MODE nc_wd;
    struct ly_set *nodeset;
    struct lyd_node *node;
    struct lyxml_elem *xml = NULL;
    struct nc_server_error *e;
    char *msg = NULL;
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    GNode *query;
    GNode *tree;

    DEBUG ("NETCONF: %s\n", rpc->schema->name);

    // TODO - check capabilities

    /* Select datastore ("get" is always RUNNING) */
    if (g_strcmp0 (rpc->schema->name, "get-config") == 0)
    {
        nodeset = lyd_find_path (rpc, "/ietf-netconf:get-config/source/*");
        if (g_strcmp0 (nodeset->set.d[0]->schema->name, "running") != 0)
        {
            msg =
                g_strdup_printf ("Datastore \"%s\" not supported",
                                 nodeset->set.d[0]->schema->name);
            ly_set_free (nodeset);
            goto error;
        }
        ly_set_free (nodeset);
    }

    /* Parse filters */
    nodeset = lyd_find_path (rpc, "/ietf-netconf:*/filter");
    if (nodeset->number)
    {
        node = nodeset->set.d[0];
        const char *type = get_attr (node, "type");
        if (g_strcmp0 (type, "xpath") == 0)
        {
            const char *path = get_attr (node, "select");
            if (!path)
            {
                msg = g_strdup_printf ("XPATH missing \"select\" attribute");
                ly_set_free (nodeset);
                err = NC_ERR_MISSING_ATTR;
                goto error;
            }
            query = xpath_to_tree (path);
        }
        else if (g_strcmp0 (type, "subtree") == 0)
        {
            LYD_ANYDATA_VALUETYPE data_type =
                ((struct lyd_node_anydata *) node)->value_type;
            struct lyxml_elem *xml;
            switch (data_type)
            {
            case LYD_ANYDATA_CONSTSTRING:
            case LYD_ANYDATA_STRING:
                {
                    xml =
                        lyxml_parse_mem (g_ctx,
                                         ((struct lyd_node_anydata *) node)->value.str,
                                         LYXML_PARSE_MULTIROOT);
                    break;
                }
            case LYD_ANYDATA_XML:
                {
                    xml = ((struct lyd_node_anydata *) node)->value.xml;
                    break;
                }
            default:
                {
                    msg =
                        g_strdup_printf ("Unsupported subtree data type \"%d\"", data_type);
                    ly_set_free (nodeset);
                    goto error;
                }
            }
            query = xml_to_tree (xml, 0);
        }
        else
        {
            msg = g_strdup_printf ("Unsupported filter type \"%s\"", type);
            ly_set_free (nodeset);
            goto error;
        }
    }
    ly_set_free (nodeset);

    /* Parse with-defaults */
    nc_server_get_capab_withdefaults (&nc_wd, NULL);
    nodeset =
        lyd_find_path (rpc, "/ietf-netconf:*/ietf-netconf-with-defaults:with-defaults");
    if (nodeset->number)
    {
        struct lyd_node_leaf_list *leaf;
        leaf = (struct lyd_node_leaf_list *) nodeset->set.d[0];
        if (g_strcmp0 (leaf->value_str, "report-all") == 0)
            nc_wd = NC_WD_ALL;
        else if (g_strcmp0 (leaf->value_str, "report-all-tagged") == 0)
            nc_wd = NC_WD_ALL_TAG;
        else if (g_strcmp0 (leaf->value_str, "trim") == 0)
            nc_wd = NC_WD_TRIM;
        else if (g_strcmp0 (leaf->value_str, "explicit") == 0)
            nc_wd = NC_WD_EXPLICIT;
    }
    ly_set_free (nodeset);

    //TODO - check read permissons

    /* Query database */
    DEBUG ("NETCONF: GET %s\n", query ? APTERYX_NAME (query) : "NULL");
    tree = query ? apteryx_query (query) : NULL;
    apteryx_free_tree (query);

    //TODO - with-defaults 

    /* Convert result to XML */
    xml = tree ? tree_to_xml (tree, 0) : NULL;
    apteryx_free_tree (tree);

    /* Send response */
    node = lyd_new_path (NULL, g_ctx, "/ietf-netconf:get/data", xml,
                         LYD_ANYDATA_XML, LYD_PATH_OPT_OUTPUT);
    return nc_server_reply_data (node, NC_WD_EXPLICIT, NC_PARAMTYPE_FREE);

  error:
    e = nc_err (err, NC_ERR_TYPE_APP);
    if (msg)
    {
        ERROR ("NETCONF: %s\n", msg);
        nc_err_set_msg (e, msg, "en");
        free (msg);
    }
    return nc_server_reply_err (e);
}

/* op_edit */
static struct nc_server_reply *
op_edit (struct lyd_node *rpc, struct nc_session *ncs)
{
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    struct nc_server_error *e;
    char *msg = NULL;
    struct ly_set *nodeset;
    GNode *tree = NULL;

    /* Check target */
    nodeset = lyd_find_path (rpc, "/ietf-netconf:edit-config/target/*");
    if (nodeset->number)
    {
        if (g_strcmp0 (nodeset->set.d[0]->schema->name, "running") != 0)
        {
            msg =
                g_strdup_printf ("Cannot edit datastore \"%s\"",
                                 nodeset->set.d[0]->schema->name);
            ly_set_free (nodeset);
            goto error;
        }
    }
    ly_set_free (nodeset);

    /* Check default-operation */
    nodeset = lyd_find_path (rpc, "/ietf-netconf:edit-config/default-operation");
    if (nodeset->number)
    {
        struct lyd_node_leaf_list *leaf;
        leaf = (struct lyd_node_leaf_list *) nodeset->set.d[0];
        if (g_strcmp0 (leaf->value_str, "merge") != 0)
        {
            msg =
                g_strdup_printf ("Do not support default-operation \"%s\"",
                                 leaf->value_str);
            ly_set_free (nodeset);
            goto error;
        }
    }
    ly_set_free (nodeset);

    /* Check test-option */
    nodeset = lyd_find_path (rpc, "/ietf-netconf:edit-config/test-option");
    if (nodeset->number)
    {
        struct lyd_node_leaf_list *leaf;
        leaf = (struct lyd_node_leaf_list *) nodeset->set.d[0];
        if (g_strcmp0 (leaf->value_str, "test-then-set") != 0)
        {
            msg = g_strdup_printf ("Do not support test-option \"%s\"", leaf->value_str);
            ly_set_free (nodeset);
            goto error;
        }
    }
    ly_set_free (nodeset);

    /* Check error-option */
    nodeset = lyd_find_path (rpc, "/ietf-netconf:edit-config/error-option");
    if (nodeset->number)
    {
        struct lyd_node_leaf_list *leaf;
        leaf = (struct lyd_node_leaf_list *) nodeset->set.d[0];
        if (g_strcmp0 (leaf->value_str, "stop-on-error") != 0)
        {
            msg = g_strdup_printf ("Do not support error-option \"%s\"", leaf->value_str);
            ly_set_free (nodeset);
            goto error;
        }
    }
    ly_set_free (nodeset);

    /* Parse config */
    nodeset = lyd_find_path (rpc, "/ietf-netconf:edit-config/config");
    if (nodeset->number)
    {
        struct lyd_node_anydata *any;
        any = (struct lyd_node_anydata *) nodeset->set.d[0];
        switch (any->value_type)
        {
        case LYD_ANYDATA_XML:
            tree = xml_to_tree (any->value.xml, 0);
            break;
        default:
            msg = g_strdup_printf ("Unsupported data type \"%d\"", any->value_type);
            ly_set_free (nodeset);
            goto error;
        }
    }
    ly_set_free (nodeset);

    //TODO - permissions
    //TODO - patterns

    /* Edit database */
    DEBUG ("NETCONF: SET %s\n", tree ? APTERYX_NAME (tree) : "NULL");
    if (tree && !apteryx_set_tree (tree))
    {
        apteryx_free_tree (tree);
        msg = g_strdup_printf ("Failed to edit config");
        err = NC_ERR_OP_FAILED;
        goto error;
    }
    apteryx_free_tree (tree);

    /* Success */
    return nc_server_reply_ok ();

  error:
    e = nc_err (err, NC_ERR_TYPE_APP);
    if (msg)
    {
        ERROR ("NETCONF: %s\n", msg);
        nc_err_set_msg (e, msg, "en");
        free (msg);
    }
    return nc_server_reply_err (e);
}

/* op_copy */
static struct nc_server_reply *
op_copy (struct lyd_node *rpc, struct nc_session *ncs)
{
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    struct nc_server_error *e;
    char *msg = NULL;
    struct ly_set *nodeset;

    /* Check datastores (only support running->startup) */
    nodeset = lyd_find_path (rpc, "/ietf-netconf:copy-config/source/*");
    if (g_strcmp0 (nodeset->set.d[0]->schema->name, "running") != 0)
    {
        msg =
            g_strdup_printf ("Cannot copy from datastore \"%s\"",
                             nodeset->set.d[0]->schema->name);
        ly_set_free (nodeset);
        goto error;
    }
    ly_set_free (nodeset);
    nodeset = lyd_find_path (rpc, "/ietf-netconf:copy-config/target/*");
    if (g_strcmp0 (nodeset->set.d[0]->schema->name, "startup") != 0)
    {
        msg =
            g_strdup_printf ("Cannot copy to datastore \"%s\"",
                             nodeset->set.d[0]->schema->name);
        ly_set_free (nodeset);
        goto error;
    }
    ly_set_free (nodeset);

    /* Copy running->startup */
    if (system (cp_cmd))
    {
        msg = g_strdup_printf ("Failed to copy running->startup");
        err = NC_ERR_OP_FAILED;
        goto error;
    }

    /* Success */
    return nc_server_reply_ok ();

  error:
    e = nc_err (err, NC_ERR_TYPE_APP);
    if (msg)
    {
        ERROR ("NETCONF: %s\n", msg);
        nc_err_set_msg (e, msg, "en");
        free (msg);
    }
    return nc_server_reply_err (e);
}

/* op_delete */
static struct nc_server_reply *
op_delete (struct lyd_node *rpc, struct nc_session *ncs)
{
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    struct nc_server_error *e;
    char *msg = NULL;
    struct ly_set *nodeset;

    /* Check datastore (onlt support startup) */
    nodeset = lyd_find_path (rpc, "/ietf-netconf:delete-config/target/*");
    if (g_strcmp0 (nodeset->set.d[0]->schema->name, "startup") != 0)
    {
        msg =
            g_strdup_printf ("Cannot delete datastore \"%s\"",
                             nodeset->set.d[0]->schema->name);
        ly_set_free (nodeset);
        goto error;
    }
    ly_set_free (nodeset);

    /* Delete startup-config */
    if (system (rm_cmd))
    {
        msg = g_strdup_printf ("Failed to delete startup-config");
        err = NC_ERR_OP_FAILED;
        goto error;
    }

    /* Success */
    return nc_server_reply_ok ();

  error:
    e = nc_err (err, NC_ERR_TYPE_APP);
    if (msg)
    {
        ERROR ("NETCONF: %s\n", msg);
        nc_err_set_msg (e, msg, "en");
        free (msg);
    }
    return nc_server_reply_err (e);
}

static void *
handle_session (void *arg)
{
    struct nc_pollsession *ps = (struct nc_pollsession *) arg;
    int rc;

    while (1)
    {
        rc = nc_ps_poll (ps, 5000, NULL);
        VERBOSE ("NETCONF: Session polling (rc=0x%x)\n", rc);
        if (rc & NC_PSPOLL_SESSION_TERM)
        {
            break;
        }
        //TODO - handle all the other returns!
    }
    VERBOSE ("NETCONF: session terminated\n");
    nc_ps_clear (ps, 0, NULL);
    //TODO - nc_ps_del_session (ps, ncs);
}

/* Thread for handling client connections */
static gpointer
netconf_accept_thread (gpointer data)
{
    GThreadPool *workers = g_thread_pool_new ((GFunc)handle_session, NULL, -1, FALSE, NULL);
    struct nc_session *ncs;
    NC_MSG_TYPE msgtype;

    usleep (1000000);
    while (g_main_loop_is_running (g_loop))
    {
        msgtype = nc_accept (500, &ncs);
        if (msgtype == NC_MSG_WOULDBLOCK)
            continue;
        if (NC_MSG_HELLO)
        {
            struct nc_pollsession *ps;

            VERBOSE ("NETCONF: New session\n");
            ps = nc_ps_new ();
            nc_ps_add_session (ps, ncs);
            g_thread_pool_push (workers, ps, NULL);
        }
        else
        {
            VERBOSE ("NETCONF: msg type %d ignored\n", msgtype);
        }
    }
    g_thread_pool_free (workers, true, false);
    return NULL;
}

static int
default_hostkey_clb (const char *name, void *user_data, char **privkey_path,
                     char **privkey_data, NC_SSH_KEY_TYPE *privkey_type)
{
    if (g_strcmp0 (name, "default") == 0)
    {
        *privkey_path = strdup ((const char *) user_data);
        return 0;
    }
    return 1;
}

static int
passwd_auth_clb (const struct nc_session *session, const char *password, void *user_data)
{
    return 0;
}

gboolean
netconf_init (const char *path, int port, const char *key, const char *cp, const char *rm)
{
    const struct lys_module *mod;
    const struct lys_node *snode;

    /* Debug */
    if (apteryx_netconf_verbose)
    {
        nc_verbosity (NC_VERB_DEBUG);
        nc_libssh_thread_verbosity (4);
    }
    else if (apteryx_netconf_debug)
    {
        nc_verbosity (NC_VERB_VERBOSE);
        nc_libssh_thread_verbosity (2);
    }
    else
    {
        nc_verbosity (NC_VERB_ERROR);
        nc_libssh_thread_verbosity (1);
    }
    nc_set_print_clb (log_cb);

    DEBUG ("NETCONF: Starting\n");
    cp_cmd = cp;
    rm_cmd = rm;

    /* Schemas */
    g_ctx = ly_ctx_new (path, 0);
    ly_ctx_load_module (g_ctx, "ietf-netconf", NULL);
    ly_ctx_load_module (g_ctx, "ietf-netconf-with-defaults", NULL);

    /* Server */
    if (nc_server_init (g_ctx))
    {
        ERROR ("NETCONF: Failed to initialise Netconf library");
        return false;
    }

    /* Set with-defaults capability basic-mode */
    nc_server_set_capab_withdefaults (NC_WD_EXPLICIT,
                                      NC_WD_ALL | NC_WD_ALL_TAG | NC_WD_TRIM |
                                      NC_WD_EXPLICIT);
    mod = ly_ctx_get_module (g_ctx, "ietf-netconf", NULL, 1);
    if (mod)
    {
        lys_features_enable (mod, "writable-running");
        lys_features_disable (mod, "candidate");
        lys_features_disable (mod, "confirmed-commit");
        lys_features_disable (mod, "rollback-on-error");
        lys_features_disable (mod, "validate");
        lys_features_enable (mod, "startup");
        lys_features_disable (mod, "url");
        lys_features_enable (mod, "xpath");
    }

    /* Set server options */
    if (nc_server_add_endpt ("main", NC_TI_LIBSSH))
    {
        ERROR ("NETCONF: Failed to create server endpoint\n");
        return false;
    }
    if (nc_server_endpt_set_address ("main", "0.0.0.0"))
    {
        ERROR ("NETCONF: Failed to configure server address\n");
        return false;
    }
    if (nc_server_endpt_set_port ("main", port))
    {
        ERROR ("NETCONF: Failed to set server port\n");
        return false;
    }
    nc_server_ssh_set_hostkey_clb (default_hostkey_clb, (void *) key, NULL);
    if (nc_server_ssh_endpt_add_hostkey ("main", "default", -1))
    {
        ERROR ("NETCONF: Failed to add server hostkey\n") return false;
    }
    nc_server_ssh_set_passwd_auth_clb (passwd_auth_clb, NULL, NULL);
    if (nc_server_ssh_endpt_set_auth_methods ("main", NC_SSH_AUTH_PASSWORD))
    {
        ERROR ("NETCONF: Failed to set server authentication\n");
        return false;
    }

    /* Setup handlers */
    snode = ly_ctx_get_node (g_ctx, NULL, "/ietf-netconf:get", 0);
    lys_set_private (snode, op_get);
    snode = ly_ctx_get_node (g_ctx, NULL, "/ietf-netconf:get-config", 0);
    lys_set_private (snode, op_get);
    snode = ly_ctx_get_node (g_ctx, NULL, "/ietf-netconf:edit-config", 0);
    lys_set_private (snode, op_edit);
    if (cp_cmd)
    {
        snode = ly_ctx_get_node (g_ctx, NULL, "/ietf-netconf:copy-config", 0);
        lys_set_private (snode, op_copy);
    }
    if (rm_cmd)
    {
        snode = ly_ctx_get_node (g_ctx, NULL, "/ietf-netconf:delete-config", 0);
        lys_set_private (snode, op_delete);
    }

    /* Create a thread for processing new sessions */
    g_thread = g_thread_new ("netconf-accept", netconf_accept_thread, NULL);

    /* Success */
    return true;
}

gboolean
netconf_shutdown (void)
{
    DEBUG ("NETCONF: Stopping\n");
    nc_server_destroy ();
    ly_ctx_destroy (g_ctx, NULL);
    return true;
}
