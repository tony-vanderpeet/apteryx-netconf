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

static struct lyd_node*
new_lyd_node(struct lyd_node *parent, struct lys_module **module, const char *name)
{
    struct lys_node *schema;
    struct lys_node *child;
    struct lyd_node* new_node = NULL;

    if (parent == NULL) {
        new_node = lyd_new (parent, *module, name);
        return new_node;
    }

    schema = parent->schema;
    for (child = schema->child; child; child = child->next) {
        if (!strcmp(child->name, name)) {
            new_node = lyd_new (parent, child->module, name);
            if (new_node != NULL) {
                *module = child->module;
                break;
            }
        }
    }
    return new_node;
}

static struct lyd_node* 
gnode_to_lydnode (struct lys_module *module, struct lyd_node *parent, GNode *node, int depth)
{
    struct lyd_node *data = NULL;
    char *name;
    bool adding_list = false;

    VERBOSE ("%*s%s\n", depth * 2, " ", APTERYX_NAME(node));

    if (depth == 0 && strlen (APTERYX_NAME (node)) == 1) {
        return gnode_to_lydnode(module, data, node->children, 1);
    } else if (depth == 0 && APTERYX_NAME( node)[0] == '/') {
        name = APTERYX_NAME (node) + 1;
    } else {
        name = APTERYX_NAME (node);
    }

    if (APTERYX_HAS_VALUE (node)) {
        /* Node is a leaf, add it to the tree. */
        data = lyd_new_leaf (parent, module, name, APTERYX_VALUE (node));
    } else if (parent != NULL && parent->schema->nodetype == LYS_LIST) {
        /* Node is a list key, just skip it. */
        data = parent;
    } else {
        data = new_lyd_node (parent, &module, name);
        if (data && data->schema->nodetype == LYS_LIST) {
            adding_list = true;
        }
    }
    if (node->children) {
        apteryx_sort_children (node, g_strcmp0);
        for (GNode *child = node->children; child; child = child->next) {
            gnode_to_lydnode (module, data, child, depth + 1);
            if (adding_list && child->next != NULL) {
                data = new_lyd_node (parent, &module, name);
            }
        }
    }

    return data;
}

static GNode *
xpath_to_query (const struct lys_module *module, const struct lys_node *yang, const char *xpath, int depth)
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
        if (!yang)
            yang = lys_getnext (NULL, NULL, module, LYS_GETNEXT_NOSTATECHECK);
        while (yang) {
            if (g_strcmp0 (yang->name, name) == 0) {
                break;
            }
            yang = lys_getnext (yang, NULL, module, LYS_GETNEXT_NOSTATECHECK);
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
            node = xpath_to_query (module, yang->child, next, depth + 1);
            if (!node) {
                g_node_destroy (rnode);
                return NULL;
            }
            g_node_prepend (child ?: rnode, node);
        }
        else if (yang->child) {
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

static GNode*
xml_to_gnode(const struct lys_module *module, const struct lys_node *yang, struct lyxml_elem *xml, int depth, bool query)
{
    GNode *tree = NULL;
    GNode *node = NULL;

    /* Find schema node */
    if (!yang)
        yang = lys_getnext (NULL, NULL, module, LYS_GETNEXT_NOSTATECHECK);
    while (yang) {
        if (g_strcmp0 (yang->name, xml->name) == 0) {
            break;
        }
        yang = lys_getnext (yang, NULL, module, LYS_GETNEXT_NOSTATECHECK);
    }
    if (yang == NULL) {
        ERROR ("ERROR: No match for %s\n", xml->name);
        return NULL;
    }

    /* Create a node */
    if (depth == 0) {
        // VERBOSE ("%*s%s\n", depth * 2, " ", "/");
        // depth++;
        // VERBOSE ("%*s%s\n", depth * 2, " ", xml->name);
        // tree = APTERYX_NODE (NULL, g_strdup ("/"));
        // node = APTERYX_NODE (tree, g_strdup (xml->name));
        VERBOSE ("%*s/%s\n", depth * 2, " ", xml->name);
        tree = node = APTERYX_NODE (NULL, g_strdup_printf ("/%s", xml->name));
    }
    else if (yang->nodetype == LYS_LIST) {
        VERBOSE ("%*s%s\n", depth * 2, " ", xml->name);
        depth++;
        tree = APTERYX_NODE (NULL, g_strdup (xml->name));
        if (xml->child && xml->child->content && strlen (xml->child->content) != 0) {
            // TODO make sure this key is the list key
            node = APTERYX_NODE (tree, g_strdup (xml->child->content));
            if (query)
                xml = xml->child;
        }
        else if (query && xml->attr) {
            // TODO make sure this key is the list key
            node = APTERYX_NODE (tree, g_strdup (xml->attr->value));
        }
        else {
            node = APTERYX_NODE (tree, g_strdup ("*"));
        }
        VERBOSE ("%*s%s\n", depth * 2, " ", APTERYX_NAME (node));
    }
    else if (yang->nodetype == LYS_LEAF) {
        tree = APTERYX_NODE (NULL, g_strdup (xml->name));
        if (xml->attr && g_strcmp0 (xml->attr->name , "operation") == 0 && g_strcmp0 (xml->attr->value, "delete") == 0) {
            APTERYX_NODE (tree, g_strdup (""));
            VERBOSE ("%*s%s = NULL\n", depth * 2, " ", xml->name);
        }
        else if (xml->content && strlen (xml->content) != 0) {
            APTERYX_NODE (tree, g_strdup (xml->content));
            VERBOSE ("%*s%s = %s\n", depth * 2, " ", xml->name, xml->content);
        }
        return tree;
    }
    else {
        VERBOSE ("%*s%s\n", depth * 2, " ", xml->name);
        tree = node = APTERYX_NODE (NULL, g_strdup_printf ("%s", xml->name));
    }

    /* Process children */
    if (xml->child) {
        yang = yang->child;
        for (struct lyxml_elem *child = xml->child; child; child = child->next)
        {
            GNode *cn = xml_to_gnode (module, yang, child, depth + 1, query);
            if (!cn) {
                ERROR ("ERROR: No child match for %s\n", child->name);
                apteryx_free_tree (tree);
                return NULL;
            }
            g_node_append (node, cn);
        }
    }
    else if (yang->child && g_strcmp0 (APTERYX_NAME (node), "*") != 0) {
        /* Get everything from here down */
        if (node)
            APTERYX_NODE (node, g_strdup ("*"));
        else
            APTERYX_NODE (tree, g_strdup ("*"));
        VERBOSE ("%*s%s\n", (depth + 1) * 2, " ", "*");
    }

    return tree;
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
    struct lys_module *module = (struct lys_module *) ly_ctx_get_module (g_ctx, "test", NULL, 0); // TODO
    NC_WD_MODE nc_wd;
    struct ly_set *nodeset;
    struct lyd_node *node;
    struct nc_server_error *e;
    char *msg = NULL;
    NC_ERR err = NC_ERR_OP_NOT_SUPPORTED;
    GNode *query = NULL;
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
            query = xpath_to_query (module, NULL, path, 0);
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
            query = xml_to_gnode (module, NULL, xml, 0, true);
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
    node = tree ? gnode_to_lydnode (module, NULL, tree, 0) : NULL;
    apteryx_free_tree (tree);

    /* Send response */
    node = lyd_new_path (NULL, g_ctx, "/ietf-netconf:get/data", node,
                         LYD_ANYDATA_DATATREE, LYD_PATH_OPT_OUTPUT);
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
    struct lys_module *module = (struct lys_module *) ly_ctx_get_module (g_ctx, "test", NULL, 0); // TODO
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
            tree = xml_to_gnode (module, NULL, any->value.xml, 0, false);
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
    const struct lys_module *mod;
    const struct lys_node *snode;
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
    g_ctx = ly_ctx_new (path, 0);
    list_schema_files (&files, path);
    for (iter = files; iter; iter = g_list_next (iter))
    {
        char *filename = (char *) iter->data;
        lys_parse_path (g_ctx, filename, LYS_IN_YANG);
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
    if (nc_server_add_endpt ("unix", NC_TI_UNIX) ||
        nc_server_endpt_set_perms ("unix", S_IRWXO, -1, -1) ||
        nc_server_endpt_set_address ("unix", unix_path))
    {
        ERROR ("NETCONF: Failed to create server endpoint\n");
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
