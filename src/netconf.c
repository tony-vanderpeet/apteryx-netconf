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
#include <fnmatch.h>
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

static GNode *
xpath_to_query (struct lys_module **module, const struct lysc_node *yang, const char *xpath, int depth)
{
    const char *next;
    GNode *node = NULL;
    GNode *rnode = NULL;
    GNode *child = NULL;
    char *name;
    char *pred;

    if (xpath && xpath[0] == '/')
    {
        xpath++;

        /* Find name */
        next = strchr (xpath, '/');
        if (next)
            name = strndup (xpath, next - xpath);
        else
            name = strdup (xpath);
        pred = strchr (name, '[');
        if (pred) {
            char *temp = strndup (name, pred - name);
            pred = strdup (pred);
            g_free (name);
            name = temp;
        }

        /* Find schema node */
        if (!(*module))
            *module = ly_ctx_get_module_implemented(g_ctx, name);
        if (!yang)
            yang = lys_getnext (NULL, NULL, module && *module ? (*module)->compiled : NULL, 0);
        while (yang) {
            if (g_strcmp0 (yang->name, name) == 0) {
                break;
            }
            yang = lys_getnext (yang, NULL, module && *module ? (*module)->compiled : NULL, 0);
        }
        if (yang == NULL) {
            ERROR ("ERROR: No match for %s\n", name);
            g_free (name);
            g_free (pred);
            return NULL;
        }

        /* Create node */
        if (depth == 0) {
            rnode = APTERYX_NODE (NULL, g_strdup_printf ("/%s", name));
            g_free (name);
        }
        else
            rnode = APTERYX_NODE (NULL, name);
        VERBOSE ("%*s%s\n", depth * 2, " ", APTERYX_NAME (rnode));

        /* TODO - properly parse predicates */
        if (pred && yang->nodetype == LYS_LIST) {
            char key[128 + 1];
            char value[128 + 1];

            if (sscanf (pred, "[%128[^=]='%128[^']']", key, value) == 2) {
                // TODO make sure this key is the list key
                child = APTERYX_NODE (NULL, g_strdup (value));
                g_node_prepend (rnode, child);
                depth++;
                VERBOSE ("%*s%s\n", depth * 2, " ", APTERYX_NAME (child));
            }
            g_free (pred);
        }

        if (next) {
            node = xpath_to_query (module, lysc_node_child (yang), next, depth + 1);
            if (!node) {
                g_node_destroy (rnode);
                return NULL;
            }
            g_node_prepend (child ?: rnode, node);
        }
        else if (lysc_node_child (yang)) {
            /* Get everything from here down if we do not already have a star */
            if (child && g_strcmp0 (APTERYX_NAME (child), "*") != 0) {
                APTERYX_NODE (child, g_strdup ("*"));
                VERBOSE ("%*s%s\n", (depth + 1) * 2, " ", "*");
            }
            else if (g_strcmp0 (APTERYX_NAME (rnode), "*") != 0) {
                APTERYX_NODE (rnode, g_strdup ("*"));
                VERBOSE ("%*s%s\n", (depth + 1) * 2, " ", "*");
            }
        }
    }

    return rnode;
}

static struct lyd_node* 
gnode_to_lydnode (struct lys_module *module, const struct lysc_node *schema, struct lyd_node *parent, GNode *node, int depth)
{
    struct lyd_node *data = NULL;
    char *name;

    /* Get the actual node name */
    if (depth == 0 && strlen (APTERYX_NAME (node)) == 1) {
        return gnode_to_lydnode(module, schema, parent, node->children, 1);
    } else if (depth == 0 && APTERYX_NAME( node)[0] == '/') {
        name = APTERYX_NAME (node) + 1;
    } else {
        name = APTERYX_NAME (node);
    }

    /* Find schema node */
    if (!schema)
        schema = lys_getnext (NULL, NULL, module ? module->compiled : NULL, 0);
    while (schema) {
        if (g_strcmp0 (schema->name, name) == 0) {
            break;
        }
        schema = lys_getnext (schema, NULL, module ? module->compiled : NULL, 0);
    }
    if (schema == NULL) {
        ERROR ("ERROR: No match for %s\n", name);
        return NULL;
    }

    /* We add list keys when we create the list */
    if (lysc_is_key(schema) && schema->nodetype == LYS_LEAF && APTERYX_HAS_VALUE (node)) {
        return NULL;
    }

    switch (schema->nodetype) {
        case LYS_CONTAINER:
            VERBOSE ("%*s%s\n", depth * 2, " ", APTERYX_NAME(node));
            lyd_new_inner(parent, module, name, 0, &data);
            apteryx_sort_children (node, g_strcmp0);
            for (GNode *child = node->children; child; child = child->next) {
                gnode_to_lydnode (module, lysc_node_child (schema), data, child, depth + 1);
            }
            break;
        case LYS_LIST:
            apteryx_sort_children (node, g_strcmp0);
            for (GNode *child = node->children; child; child = child->next) {
                VERBOSE ("%*s%s[%s]\n", depth * 2, " ", APTERYX_NAME(node), APTERYX_NAME(child));
                lyd_new_list(parent, module, name, 0, &data, APTERYX_NAME(child));
                for (GNode *field = child->children; field; field = field->next) {
                    gnode_to_lydnode (module, lysc_node_child (schema), data, field, depth + 1);
                }
            }
            break;
        case LYS_LEAF:
            if (!APTERYX_HAS_VALUE (node)) {
                ERROR ("ERROR: Leaf node (%s) has not data\n", name);
                return NULL;
            }
            VERBOSE ("%*s%s = %s\n", depth * 2, " ", APTERYX_NAME(node), APTERYX_VALUE(node));
            lyd_new_term (parent, module, name, APTERYX_VALUE (node), 0, &data);
            break;
        default:
            ERROR ("ERROR: Unsupported type %d for node %s\n", schema->nodetype, schema->name);
            return NULL;
    }

    return data;
}

static GNode *
lydnode_to_gnode (struct lys_module *module, const struct lysc_node *schema, GNode *parent, struct lyd_node *lydnode, int depth)
{
    const char *name = LYD_NAME(lydnode);
    struct lyd_node *child;
    GNode *tree = NULL;
    GNode *node = NULL;

    /* Find schema node */
    if (!schema)
        schema = lys_getnext (NULL, NULL, module ? module->compiled : NULL, 0);
    while (schema) {
        if (g_strcmp0 (schema->name, name) == 0) {
            break;
        }
        schema = lys_getnext (schema, NULL, module ? module->compiled : NULL, 0);
    }
    if (schema == NULL) {
        ERROR ("ERROR: No match for %s\n", name);
        return NULL;
    }

    switch (schema->nodetype) {
        case LYS_CONTAINER:
            VERBOSE ("%*s%s%s\n", depth * 2, " ", depth ? "" : "/", name);
            tree = node = APTERYX_NODE (NULL, g_strdup_printf ("%s%s", depth ? "" : "/", name));
            for (child = lyd_child(lydnode); child; child = child->next) {
                GNode *cn = lydnode_to_gnode (module, lysc_node_child (schema), NULL, child, depth + 1);
                if (!cn) {
                    ERROR ("ERROR: No child match for %s\n", lysc_node_child (schema)->name);
                    apteryx_free_tree (node);
                    return NULL;
                }
                g_node_append (node, cn);
            }
            break;
        case LYS_LIST:
            VERBOSE ("%*s%s%s\n", depth * 2, " ", depth ? "" : "/", name);
            depth++;
            tree = node = APTERYX_NODE (NULL, g_strdup (name));
            if (lyd_child(lydnode) && lysc_is_key(lyd_child(lydnode)->schema) &&
                strlen(((struct lyd_node_any *) lyd_child(lydnode))->value.str) > 0) {
                node = APTERYX_NODE (node, g_strdup (((struct lyd_node_any *) lyd_child(lydnode))->value.str));
            } else {
                node = APTERYX_NODE (node, g_strdup ("*"));
            }
            VERBOSE ("%*s%s\n", depth * 2, " ", APTERYX_NAME(node));
            for (child = lyd_child(lydnode); child; child = child->next) {
                if (lysc_is_key(lysc_node_child (schema)) && lysc_node_child (schema)->nodetype == LYS_LEAF &&
                    strlen(((struct lyd_node_any *) lyd_child(lydnode))->value.str) > 0)
                    continue;
                GNode *cn = lydnode_to_gnode (module, lysc_node_child (schema), NULL, child, depth + 1);
                if (!cn) {
                    ERROR ("ERROR: No child match for %s\n", child->schema->name);
                    apteryx_free_tree (node);
                    return NULL;
                }
                g_node_append (node, cn);
            }
            if (lysc_is_key(lysc_node_child (schema)) && lysc_node_child (schema)->nodetype == LYS_LEAF)
                lydnode = lyd_child(lydnode);
            break;
        case LYS_LEAF:
        {
            const struct lyd_node_any *any = (struct lyd_node_any *)lydnode;
            struct lyd_meta *meta = meta = lyd_find_meta(lydnode->meta, NULL, "ietf-netconf:operation");
            tree = node = APTERYX_NODE (NULL, g_strdup (name));
            if (meta && !g_strcmp0 (lyd_get_meta_value (meta), "delete")) {
                APTERYX_NODE (tree, g_strdup (""));
                VERBOSE ("%*s%s = NULL\n", depth * 2, " ", name);
            }
            else if (any->value.str && any->value.str[0]) {
                APTERYX_NODE (tree, g_strdup (any->value.str));
                VERBOSE ("%*s%s = %s\n", depth * 2, " ", name, any->value.str);
            } else {
                VERBOSE ("%*s%s%s\n", depth * 2, " ", depth ? "" : "/", name);
            }
            break;
        }
        default:
            ERROR ("ERROR: Unsupported type %d for node %s\n", schema->nodetype, schema->name);
            return NULL;
    }

    /* Get everything from here down if a trunk of a subtree */
    if (!lyd_child(lydnode) && lysc_node_child (schema) && g_strcmp0 (APTERYX_NAME (node), "*") != 0) {
        APTERYX_NODE (node, g_strdup ("*"));
        VERBOSE ("%*s%s\n", (depth + 1) * 2, " ", "*");
    }
    return tree;
}

/* op_get */
static struct nc_server_reply *
op_get (struct lyd_node *rpc, struct nc_session *ncs)
{
    struct lys_module *module = NULL;
    NC_WD_MODE nc_wd;
    struct ly_set *nodeset;
    struct lyd_node *node;
    struct lyd_node *output;
    char *msg = NULL;
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    GNode *query = NULL;
    GNode *tree;

    DEBUG ("NETCONF: %s\n", rpc->schema->name);

    // TODO - check capabilities

    /* Select datastore ("get" is always RUNNING) */
    if (g_strcmp0 (rpc->schema->name, "get-config") == 0)
    {
        lyd_find_xpath (rpc, "/ietf-netconf:get-config/source/*", &nodeset);
        if (g_strcmp0 (nodeset->dnodes[0]->schema->name, "running") != 0)
        {
            msg =
                g_strdup_printf ("Datastore \"%s\" not supported",
                                 nodeset->dnodes[0]->schema->name);
            ly_set_free (nodeset, NULL);
            goto error;
        }
        ly_set_free (nodeset, NULL);
    }

    /* Parse filters */
    lyd_find_xpath (rpc, "/ietf-netconf:*/filter", &nodeset);
    if (nodeset->count)
    {
        node = nodeset->dnodes[0];
        struct lyd_meta *meta = lyd_find_meta (node->meta, NULL, "ietf-netconf:type");
        if (meta && !g_strcmp0 (lyd_get_meta_value (meta), "xpath"))
        {
            meta = lyd_find_meta (node->meta, NULL, "ietf-netconf:select");
            if (!meta)
            {
                msg = g_strdup_printf ("XPATH missing \"select\" attribute");
                ly_set_free (nodeset, NULL);
                err = NC_ERR_MISSING_ATTR;
                goto error;
            }
            query = xpath_to_query (&module, NULL, lyd_get_meta_value (meta), 0);
        }
        else if (meta && !g_strcmp0 (lyd_get_meta_value (meta), "subtree"))
        {
            LYD_ANYDATA_VALUETYPE data_type =
                ((struct lyd_node_any *) node)->value_type;
            switch (data_type)
            {
            case LYD_ANYDATA_DATATREE:
                {
                    struct lyd_node *lydtree = ((struct lyd_node_any *)node)->value.tree;
                    module = lydtree && lydtree->schema ? lydtree->schema->module : NULL;
                    query = lydnode_to_gnode (module, NULL, NULL, lydtree, 0);
                    break;
                }
            default:
                {
                    ERROR ("ERROR: Unsupported subtree data type \"%d\"\n", data_type);
                    msg =
                        g_strdup_printf ("Unsupported subtree data type \"%d\"", data_type);
                    ly_set_free (nodeset, NULL);
                    goto error;
                }
            }
        }
        else
        {
            ERROR ("ERROR: Unsupported filter type \"%s\"\n", lyd_get_meta_value (meta));
            msg = g_strdup_printf ("Unsupported filter type \"%s\"", lyd_get_meta_value (meta));
            ly_set_free (nodeset, NULL);
            goto error;
        }
    }
    ly_set_free (nodeset, NULL);

    /* Parse with-defaults */
    nc_server_get_capab_withdefaults (&nc_wd, NULL);
    lyd_find_xpath (rpc, "/ietf-netconf:*/ietf-netconf-with-defaults:with-defaults", &nodeset);
    if (nodeset->count)
    {
        node = nodeset->dnodes[0];
        if (g_strcmp0 (lyd_get_value(node), "report-all") == 0)
            nc_wd = NC_WD_ALL;
        else if (g_strcmp0 (lyd_get_value(node), "report-all-tagged") == 0)
            nc_wd = NC_WD_ALL_TAG;
        else if (g_strcmp0 (lyd_get_value(node), "trim") == 0)
            nc_wd = NC_WD_TRIM;
        else if (g_strcmp0 (lyd_get_value(node), "explicit") == 0)
            nc_wd = NC_WD_EXPLICIT;
    }
    ly_set_free (nodeset, NULL);

    //TODO - check read permissons

    /* Query database */
    DEBUG ("NETCONF: GET %s\n", query ? APTERYX_NAME (query) : "NULL");
    tree = query ? apteryx_query (query) : NULL;
    apteryx_free_tree (query);

    //TODO - with-defaults 

    /* Convert result to XML */
    node = tree ? gnode_to_lydnode (module, NULL, NULL, tree, 0) : NULL;
    apteryx_free_tree (tree);

    /* Send response */
    lyd_new_path (NULL, g_ctx, "/ietf-netconf:get", NULL, LYD_NEW_PATH_OUTPUT, &output);
    lyd_new_any (output, NULL, "data", node, 1, LYD_ANYDATA_DATATREE, 1, NULL);
    return nc_server_reply_data (output, NC_WD_EXPLICIT, NC_PARAMTYPE_FREE);

  error:
    node = nc_err (g_ctx, err, NC_ERR_TYPE_APP);
    if (msg)
    {
        ERROR ("NETCONF: %s\n", msg);
        nc_err_set_msg (node, msg, "en");
        free (msg);
    }
    return nc_server_reply_err (node);
}

/* op_edit */
static struct nc_server_reply *
op_edit (struct lyd_node *rpc, struct nc_session *ncs)
{
    struct lys_module *module = NULL;
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    char *msg = NULL;
    struct ly_set *nodeset;
    struct lyd_node *node;
    GNode *tree = NULL;

    /* Check target */
    lyd_find_xpath (rpc, "/ietf-netconf:edit-config/target/*", &nodeset);
    if (nodeset->count)
    {
        if (g_strcmp0 (nodeset->dnodes[0]->schema->name, "running") != 0)
        {
            msg =
                g_strdup_printf ("Cannot edit datastore \"%s\"",
                                 nodeset->dnodes[0]->schema->name);
            ly_set_free (nodeset, NULL);
            goto error;
        }
    }
    ly_set_free (nodeset, NULL);

    /* Check default-operation */
    lyd_find_xpath (rpc, "/ietf-netconf:edit-config/default-operation", &nodeset);
    if (nodeset->count)
    {
        node = nodeset->dnodes[0];
        if (g_strcmp0 (lyd_get_value(node), "merge") != 0)
        {
            msg =
                g_strdup_printf ("Do not support default-operation \"%s\"",
                                 lyd_get_value(node));
            ly_set_free (nodeset, NULL);
            goto error;
        }
    }
    ly_set_free (nodeset, NULL);

    /* Check test-option */
    lyd_find_xpath (rpc, "/ietf-netconf:edit-config/test-option", &nodeset);
    if (nodeset->count)
    {
        node = nodeset->dnodes[0];
        if (g_strcmp0 (lyd_get_value(node), "test-then-set") != 0)
        {
            msg = g_strdup_printf ("Do not support test-option \"%s\"", lyd_get_value(node));
            ly_set_free (nodeset, NULL);
            goto error;
        }
    }
    ly_set_free (nodeset, NULL);

    /* Check error-option */
    lyd_find_xpath (rpc, "/ietf-netconf:edit-config/error-option", &nodeset);
    if (nodeset->count)
    {
        node = nodeset->dnodes[0];
        if (g_strcmp0 (lyd_get_value(node), "stop-on-error") != 0)
        {
            msg = g_strdup_printf ("Do not support test-option \"%s\"", lyd_get_value(node));
            ly_set_free (nodeset, NULL);
            goto error;
        }
    }
    ly_set_free (nodeset, NULL);

    /* Parse config */
    lyd_find_xpath (rpc, "/ietf-netconf:edit-config/config", &nodeset);
    if (nodeset->count)
    {
        struct lyd_node_any *any = (struct lyd_node_any *) nodeset->dnodes[0];
        switch (any->value_type)
        {
        case LYD_ANYDATA_DATATREE:
            {
                struct lyd_node *lydtree = any->value.tree;
                module = lydtree && lydtree->schema ? lydtree->schema->module : NULL;
                tree = lydnode_to_gnode (module, NULL, NULL, lydtree, 0);
                break;
            }
        default:
            msg = g_strdup_printf ("Unsupported data type \"%d\"", any->value_type);
            ly_set_free (nodeset, NULL);
            goto error;
        }
    }
    ly_set_free (nodeset, NULL);

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
    node = nc_err (g_ctx, err, NC_ERR_TYPE_APP);
    if (msg)
    {
        ERROR ("NETCONF: %s\n", msg);
        nc_err_set_msg (node, msg, "en");
        free (msg);
    }
    return nc_server_reply_err (node);
}

/* op_copy */
static struct nc_server_reply *
op_copy (struct lyd_node *rpc, struct nc_session *ncs)
{
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    // struct nc_server_error *e;
    char *msg = NULL;
    struct ly_set *nodeset;
    struct lyd_node *node;

    /* Check datastores (only support running->startup) */
    lyd_find_xpath (rpc, "/ietf-netconf:copy-config/source/*", &nodeset);
    if (g_strcmp0 (nodeset->dnodes[0]->schema->name, "running") != 0)
    {
        msg =
            g_strdup_printf ("Cannot copy from datastore \"%s\"",
                             nodeset->dnodes[0]->schema->name);
        ly_set_free (nodeset, NULL);
        goto error;
    }
    ly_set_free (nodeset, NULL);
    lyd_find_xpath (rpc, "/ietf-netconf:copy-config/target/*", &nodeset);
    if (g_strcmp0 (nodeset->dnodes[0]->schema->name, "startup") != 0)
    {
        msg =
            g_strdup_printf ("Cannot copy to datastore \"%s\"",
                             nodeset->dnodes[0]->schema->name);
        ly_set_free (nodeset, NULL);
        goto error;
    }
    ly_set_free (nodeset, NULL);

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
    node = nc_err (g_ctx, err, NC_ERR_TYPE_APP);
    if (msg)
    {
        ERROR ("NETCONF: %s\n", msg);
        nc_err_set_msg (node, msg, "en");
        free (msg);
    }
    return nc_server_reply_err (node);
}

/* op_delete */
static struct nc_server_reply *
op_delete (struct lyd_node *rpc, struct nc_session *ncs)
{
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    // struct nc_server_error *e;
    char *msg = NULL;
    struct ly_set *nodeset;
    struct lyd_node *node;

    /* Check datastore (onlt support startup) */
    lyd_find_xpath (rpc, "/ietf-netconf:delete-config/target/*", &nodeset);
    if (g_strcmp0 (nodeset->dnodes[0]->schema->name, "startup") != 0)
    {
        msg =
            g_strdup_printf ("Cannot delete datastore \"%s\"",
                             nodeset->dnodes[0]->schema->name);
        ly_set_free (nodeset, NULL);
        goto error;
    }
    ly_set_free (nodeset, NULL);

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
    node = nc_err (g_ctx, err, NC_ERR_TYPE_APP);
    if (msg)
    {
        ERROR ("NETCONF: %s\n", msg);
        nc_err_set_msg (node, msg, "en");
        free (msg);
    }
    return nc_server_reply_err (node);
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
    return NULL;
}

/* Thread for handling client connections */
static gpointer
netconf_accept_thread (gpointer data)
{
    GThreadPool *workers = g_thread_pool_new ((GFunc)handle_session, NULL, -1, FALSE, NULL);
    struct nc_session *ncs;
    NC_MSG_TYPE msgtype;

    usleep (1000000);
    VERBOSE ("NETCONF: Accepting client connections\n");
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
    VERBOSE ("NETCONF: Finished accepting clients\n");
    return NULL;
}

static void
list_schema_files (GList **files, const char *path)
{
    DIR *dp;
    struct dirent *ep;
    char *saveptr = NULL;
    char *cpath;
    char *dpath;

    cpath = strdup (path);
    dpath = strtok_r (cpath, ":", &saveptr);
    while (dpath != NULL)
    {
        dp = opendir (dpath);
        if (dp != NULL)
        {
            while ((ep = readdir (dp)))
            {
                if (true && fnmatch ("*.yang", ep->d_name, 0) != 0)
                {
                    continue;
                }
                *files = g_list_append (*files, g_strdup_printf ("%s/%s", dpath, ep->d_name));
            }
            (void) closedir (dp);
        }
        dpath = strtok_r (NULL, ":", &saveptr);
    }
    free (cpath);
    *files = g_list_sort (*files, (GCompareFunc) strcasecmp);
    return;
}

gboolean
netconf_init (const char *path, int port, const char *unix_path, const char *cp, const char *rm)
{
    struct lys_module *mod;
    struct lysc_node *rpc;
    GList *files = NULL;
    GList *iter;

    /* Debug */
    if (apteryx_netconf_verbose)
    {
        nc_verbosity (NC_VERB_DEBUG);
    }
    else if (apteryx_netconf_debug)
    {
        nc_verbosity (NC_VERB_VERBOSE);
    }
    else
    {
        nc_verbosity (NC_VERB_ERROR);
    }
    nc_set_print_clb (log_cb);

    DEBUG ("NETCONF: Starting\n");
    cp_cmd = cp;
    rm_cmd = rm;

    /* Schemas */
    if (ly_ctx_new (path, 0, &g_ctx))
    {
        ERROR ("NETCONF: Failed to create libyang context");
        return false;
    }
    ly_ctx_load_module (g_ctx, "ietf-netconf", NULL, NULL);
    ly_ctx_load_module (g_ctx, "ietf-netconf-with-defaults", NULL, NULL);
    list_schema_files (&files, path);
    for (iter = files; iter; iter = g_list_next (iter))
    {
        char *filename = (char *) iter->data;
        DEBUG ("NETCONF: Loading %s\n", filename);
        lys_parse_path (g_ctx, filename, LYS_IN_YANG, &mod);
    }
    g_list_free_full (files, free);

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
    mod = ly_ctx_get_module_implemented(g_ctx, "ietf-netconf");
    // if (mod)
    // {
    //     lys_features_enable (mod, "writable-running");
    //     lys_features_disable (mod, "candidate");
    //     lys_features_disable (mod, "confirmed-commit");
    //     lys_features_disable (mod, "rollback-on-error");
    //     lys_features_disable (mod, "validate");
    //     lys_features_enable (mod, "startup");
    //     lys_features_disable (mod, "url");
    //     lys_features_enable (mod, "xpath");
    // }

    /* Set server options */
    if (nc_server_add_endpt ("unix", NC_TI_UNIX) ||
        nc_server_endpt_set_perms ("unix", S_IRWXO, -1, -1) ||
        nc_server_endpt_set_address ("unix", unix_path))
    {
        ERROR ("NETCONF: Failed to create server endpoint\n");
        return false;
    }

    /* Setup handlers */
    rpc = (struct lysc_node *)lys_find_path (g_ctx, NULL, "/ietf-netconf:get", 0);
    nc_set_rpc_callback (rpc, op_get);
    rpc = (struct lysc_node *)lys_find_path (g_ctx, NULL, "/ietf-netconf:get-config", 0);
    nc_set_rpc_callback (rpc, op_get);
    rpc = (struct lysc_node *)lys_find_path (g_ctx, NULL, "/ietf-netconf:edit-config", 0);
    nc_set_rpc_callback (rpc, op_edit);
    if (cp_cmd)
    {
        rpc = (struct lysc_node *)lys_find_path (g_ctx, NULL, "/ietf-netconf:copy-config", 0);
        nc_set_rpc_callback (rpc, op_copy);
    }
    if (rm_cmd)
    {
        rpc = (struct lysc_node *)lys_find_path (g_ctx, NULL, "/ietf-netconf:delete-config", 0);
        nc_set_rpc_callback (rpc, op_delete);
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
    ly_ctx_destroy (g_ctx);
    return true;
}
