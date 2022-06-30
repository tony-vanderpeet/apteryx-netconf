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
#include <sys/socket.h>
#define APTERYX_XML_LIBXML2
#include <apteryx-xml.h>

static sch_instance *g_schema = NULL;

struct netconf_session
{
    int fd;
};

typedef struct _edit_config_parameters
{
    sch_instance * in_instance;
    xmlNode * in_xml;
    int in_def_op;
    int in_flags;
    GNode * out_set_tree;
    GList * out_delete_paths;
    char * error_tag;
} edit_config_parameters;

#define NETCONF_BASE_1_0_END "]]>]]>"
#define NETCONF_BASE_1_1_END "\n##\n"

/* edit-config operations */
#define NC_OP_NONE      0
#define NC_OP_MERGE     1
#define NC_OP_REPLACE   2
#define NC_OP_CREATE    3
#define NC_OP_DELETE    4
#define NC_OP_REMOVE    5

static bool
send_rpc_ok (struct netconf_session *session, xmlNode * rpc)
{
    xmlDoc *doc;
    xmlNode *root;
    xmlChar *xmlbuff = NULL;
    char *header = NULL;
    int len;
    bool ret = true;

    /* Generate reply */
    doc = xmlNewDoc (BAD_CAST "1.0");
    root = xmlNewNode (NULL, BAD_CAST "nc:rpc-reply");
    xmlFreePropList (root->properties);
    root->properties = xmlCopyPropList (root, rpc->properties);
    xmlNewProp (root, BAD_CAST "xmlns:nc",
                BAD_CAST "urn:ietf:params:xml:ns:netconf:base:1.0");
    xmlDocSetRootElement (doc, root);
    xmlNewChild (root, NULL, BAD_CAST "nc:ok", NULL);
    xmlDocDumpMemoryEnc (doc, &xmlbuff, &len, "UTF-8");
    header = g_strdup_printf ("\n#%d\n", len);

    /* Send reply */
    if (write (session->fd, header, strlen (header)) != strlen (header))
    {
        ERROR ("TX failed: Sending %ld bytes of header\n", strlen (header));
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%ld):\n%s", strlen (header), header);
    if (write (session->fd, xmlbuff, len) != len)
    {
        ERROR ("TX failed: Sending %d bytes of hello\n", len);
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%d):\n%.*s", len, len, (char *) xmlbuff);
    if (write (session->fd, NETCONF_BASE_1_1_END, strlen (NETCONF_BASE_1_1_END)) !=
        strlen (NETCONF_BASE_1_1_END))
    {
        ERROR ("TX failed: Sending %ld bytes of trailer\n", strlen (NETCONF_BASE_1_1_END));
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%ld):\n%s\n", strlen (NETCONF_BASE_1_1_END), NETCONF_BASE_1_1_END);

  cleanup:
    g_free (header);
    xmlFree (xmlbuff);
    xmlFreeDoc (doc);
    return ret;
}

static bool
send_rpc_error (struct netconf_session *session, xmlNode * rpc, const char *error)
{
    xmlDoc *doc;
    xmlNode *root, *child;
    xmlChar *xmlbuff = NULL;
    char *header = NULL;
    int len;
    bool ret = true;

    /* Generate reply */
    doc = xmlNewDoc (BAD_CAST "1.0");
    root = xmlNewNode (NULL, BAD_CAST "nc:rpc-reply");
    xmlFreePropList (root->properties);
    root->properties = xmlCopyPropList (root, rpc->properties);
    xmlNewProp (root, BAD_CAST "xmlns:nc",
                BAD_CAST "urn:ietf:params:xml:ns:netconf:base:1.0");
    xmlDocSetRootElement (doc, root);
    child = xmlNewChild (root, NULL, BAD_CAST "nc:rpc-error", NULL);
    xmlNewChild (child, NULL, BAD_CAST "nc:error-tag", BAD_CAST error);
    xmlNewChild (child, NULL, BAD_CAST "nc:error-type", BAD_CAST "rpc");
    xmlNewChild (child, NULL, BAD_CAST "nc:error-severity", BAD_CAST "error");
    xmlNewChild (child, NULL, BAD_CAST "nc:error-info", BAD_CAST NULL);
    xmlDocDumpMemoryEnc (doc, &xmlbuff, &len, "UTF-8");
    header = g_strdup_printf ("\n#%d\n", len);

    /* Send reply */
    if (write (session->fd, header, strlen (header)) != strlen (header))
    {
        ERROR ("TX failed: Sending %ld bytes of header\n", strlen (header));
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%ld):\n%s", strlen (header), header);
    if (write (session->fd, xmlbuff, len) != len)
    {
        ERROR ("TX failed: Sending %d bytes of hello\n", len);
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%d):\n%.*s", len, len, (char *) xmlbuff);
    if (write (session->fd, NETCONF_BASE_1_1_END, strlen (NETCONF_BASE_1_1_END)) !=
        strlen (NETCONF_BASE_1_1_END))
    {
        ERROR ("TX failed: Sending %ld bytes of trailer\n", strlen (NETCONF_BASE_1_1_END));
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%ld):\n%s\n", strlen (NETCONF_BASE_1_1_END), NETCONF_BASE_1_1_END);

  cleanup:
    g_free (header);
    xmlFree (xmlbuff);
    xmlFreeDoc (doc);
    return ret;
}

static bool
send_rpc_data (struct netconf_session *session, xmlNode * rpc, xmlNode * data)
{
    xmlDoc *doc;
    xmlNode *root, *child;
    xmlChar *xmlbuff = NULL;
    char *header = NULL;
    int len;
    bool ret = true;

    /* Generate reply */
    doc = xmlNewDoc (BAD_CAST "1.0");
    root = xmlNewNode (NULL, BAD_CAST "nc:rpc-reply");
    xmlFreePropList (root->properties);
    root->properties = xmlCopyPropList (root, rpc->properties);
    xmlNewProp (root, BAD_CAST "xmlns:nc",
                BAD_CAST "urn:ietf:params:xml:ns:netconf:base:1.0");
    xmlDocSetRootElement (doc, root);
    child = xmlNewChild (root, NULL, BAD_CAST "nc:data", NULL);
    xmlNewProp (child, BAD_CAST "xmlns:nc",
                BAD_CAST "urn:ietf:params:xml:ns:netconf:base:1.0");
    xmlAddChildList (child, data);
    xmlDocDumpMemoryEnc (doc, &xmlbuff, &len, "UTF-8");
    header = g_strdup_printf ("\n#%d\n", len);

    /* Send reply */
    if (write (session->fd, header, strlen (header)) != strlen (header))
    {
        ERROR ("TX failed: Sending %ld bytes of header\n", strlen (header));
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%ld):\n%s", strlen (header), header);
    if (write (session->fd, xmlbuff, len) != len)
    {
        ERROR ("TX failed: Sending %d bytes of hello\n", len);
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%d):\n%.*s", len, len, (char *) xmlbuff);
    if (write (session->fd, NETCONF_BASE_1_1_END, strlen (NETCONF_BASE_1_1_END)) !=
        strlen (NETCONF_BASE_1_1_END))
    {
        ERROR ("TX failed: Sending %ld bytes of trailer\n", strlen (NETCONF_BASE_1_1_END));
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%ld):\n%s\n", strlen (NETCONF_BASE_1_1_END), NETCONF_BASE_1_1_END);

  cleanup:
    g_free (header);
    xmlFree (xmlbuff);
    xmlFreeDoc (doc);
    return ret;
}

static bool
handle_hello (struct netconf_session *session)
{
    bool ret = true;
    xmlDoc *doc = NULL;
    xmlNode *root, *node, *child;
    xmlChar *xmlbuff;
    char buffer[4096];
    char *endpt;
    int len;

    /* Read all of the hello from the peer */
    while (g_main_loop_is_running (g_loop))
    {
        len = recv (session->fd, buffer, 4096, 0);
        // TODO
        break;
    }
    VERBOSE ("RX(%d):\n%.*s", len, (int) len, buffer);

    /* Find trailer */
    endpt = g_strstr_len (buffer, len, NETCONF_BASE_1_0_END);
    if (!endpt)
    {
        ERROR ("XML: Invalid hello message (no 1.0 trailer)\n");
        return false;
    }

    /* Validate hello */
    doc = xmlParseMemory (buffer, (endpt - buffer));
    if (!doc)
    {
        ERROR ("XML: Invalid hello message\n");
        return false;
    }
    root = xmlDocGetRootElement (doc);
    if (!root || g_strcmp0 ((char *) root->name, "hello") != 0)
    {
        ERROR ("XML: No root HELLO element\n");
        xmlFreeDoc (doc);
        return len;
    }
    // TODO check capabilities
    // :base:1.1

    VERBOSE ("Received valid hello message\n");
    xmlFreeDoc (doc);

    /* Generate reply */
    doc = xmlNewDoc (BAD_CAST "1.0");
    root = xmlNewNode (NULL, BAD_CAST "nc:hello");
    xmlNewProp (root, BAD_CAST "xmlns:nc",
                BAD_CAST "urn:ietf:params:xml:ns:netconf:base:1.0");
    xmlDocSetRootElement (doc, root);
    node = xmlNewChild (root, NULL, BAD_CAST "nc:capabilities", NULL);
    child = xmlNewChild (node, NULL, BAD_CAST "nc:capability", NULL);
    xmlNodeSetContent (child, BAD_CAST "urn:ietf:params:netconf:base:1.1");
    child = xmlNewChild (node, NULL, BAD_CAST "nc:capability", NULL);
    xmlNodeSetContent (child, BAD_CAST "urn:ietf:params:netconf:capability:xpath:1.0");
    child = xmlNewChild (node, NULL, BAD_CAST "nc:capability", NULL);
    xmlNodeSetContent (child, BAD_CAST "urn:ietf:params:netconf:capability:writable-running:1.0");
    child = xmlNewChild (node, NULL, BAD_CAST "nc:capability", NULL);
    xmlNodeSetContent (child,
                       BAD_CAST "urn:ietf:params:netconf:capability:with-defaults:1.0");
    xmlDocDumpMemoryEnc (doc, &xmlbuff, &len, "UTF-8");

    /* Send reply */
    if (write (session->fd, xmlbuff, len) != len)
    {
        ERROR ("TX failed: Sending %d bytes of hello\n", len);
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%d):\n%.*s", len, len, (char *) xmlbuff);
    if (write (session->fd, NETCONF_BASE_1_0_END, strlen (NETCONF_BASE_1_0_END)) !=
        strlen (NETCONF_BASE_1_0_END))
    {
        ERROR ("TX failed: Sending %ld bytes of hello trailer\n",
               strlen (NETCONF_BASE_1_0_END));
        ret = false;
        goto cleanup;
    }
    VERBOSE ("TX(%ld):\n%s\n", strlen (NETCONF_BASE_1_0_END), NETCONF_BASE_1_0_END);

  cleanup:
    xmlFree (xmlbuff);
    xmlFreeDoc (doc);
    return ret;
}

static GNode*
get_full_tree ()
{
    GNode *tree = APTERYX_NODE (NULL, g_strdup_printf ("/"));
    GList *children, *iter;

    /* Search root and then get tree for each root entry */
    children = apteryx_search ("/");
    for (iter = children; iter; iter = g_list_next (iter))
    {
        const char *path = (const char *) iter->data;
        GNode *subtree = apteryx_get_tree (path);
        if (subtree)
        {
            g_free (subtree->data);
            subtree->data = g_strdup (path + 1);
            g_node_append (tree, subtree);
        }
    }
    g_list_free_full (children, free);
    return tree;
}

static bool
handle_get (struct netconf_session *session, xmlNode * rpc, gboolean config_only)
{
    xmlNode *action = xmlFirstElementChild (rpc);
    xmlNode *node;
    char *attr;
    GNode *query = NULL;
    GNode *tree;
    xmlNode *xml = NULL;
    int schflags = 0;

    if (apteryx_netconf_verbose)
        schflags |= SCH_F_DEBUG;

    if (config_only)
    {
        schflags |= SCH_F_CONFIG;
    }

    /* Parse options */
    for (node = xmlFirstElementChild (action); node; node = xmlNextElementSibling (node))
    {
        /* Check the requested datastore */
        if (g_strcmp0 ((char *) node->name, "source") == 0)
        {
            if (!xmlFirstElementChild (node) ||
                g_strcmp0 ((char *) xmlFirstElementChild (node)->name, "running") != 0)
            {
                VERBOSE ("Datastore \"%s\" not supported",
                        (char *) xmlFirstElementChild (node)->name);
                return send_rpc_error (session, rpc, "operation-not-supported");
            }
        }
        /* Parse any filters */
        else if (g_strcmp0 ((char *) node->name, "filter") == 0)
        {
            attr = (char *) xmlGetProp (node, BAD_CAST "type");
            if (g_strcmp0 (attr, "xpath") == 0)
            {
                free (attr);
                attr = (char *) xmlGetProp (node, BAD_CAST "select");
                if (!attr)
                {
                    VERBOSE ("XPATH filter missing select attribute");
                    return send_rpc_error (session, rpc, "missing-attribute");
                }
                VERBOSE ("FILTER: XPATH: %s\n", attr);
                query = sch_path_to_query (g_schema, NULL, attr, schflags | SCH_F_XPATH);
            }
            else if (g_strcmp0 (attr, "subtree") == 0)
            {
                if (!xmlFirstElementChild (node)) {
                    VERBOSE ("SUBTREE: empty query\n");
                    free (attr);
                    return send_rpc_data (session, rpc, NULL);
                }
                query = sch_xml_to_gnode (g_schema, NULL, xmlFirstElementChild (node), SCH_F_STRIP_KEY);
                if (!query)
                {
                    VERBOSE ("SUBTREE: malformed query\n");
                    free (attr);
                    return send_rpc_error (session, rpc, "malformed-message");
                }
            }
            else
            {
                VERBOSE ("FILTER: unsupported/missing type (%s)\n", attr);
                free (attr);
                return send_rpc_error (session, rpc, "operation-not-supported");
            }
            free (attr);
        }
        //TODO - Parse with-defaults
    }

    /* Query database */
    DEBUG ("NETCONF: GET %s\n", query ? APTERYX_NAME (query) : "/");
    tree = query ? apteryx_query (query) : get_full_tree ();
    apteryx_free_tree (query);

    /* Convert result to XML */
    xml = tree ? sch_gnode_to_xml (g_schema, NULL, tree, schflags) : NULL;
    apteryx_free_tree (tree);

    /* Send response */
    send_rpc_data (session, rpc, xml);

    return true;
}

static xmlNode *
xmlFindNodeByName (xmlNode * root, const xmlChar * name)
{
    xmlNode *child;

    for (child = xmlFirstElementChild (root); child; child = xmlNextElementSibling (child))
    {
        if (!xmlStrcmp (child->name, name))
        {
            return child;
        }
    }
    return NULL;
}

static bool
xml_node_has_content (xmlNode * xml)
{
    xmlChar *content = xmlNodeGetContent (xml);
    bool ret = (content && xmlStrlen (content) > 0);
    xmlFree (content);
    return ret;
}


/**
 * Check XML node for the operation attribute and extract it. Return whether the
 * operation is recognised or not.
 */
static bool
_operation_ok (xmlNode *xml, int curr_op, int *new_op, edit_config_parameters *parms)
{
    char *attr;

    attr = (char *) xmlGetProp (xml, BAD_CAST "operation");
    if (attr != NULL)
    {
        if (g_strcmp0 (attr, "delete") == 0)
        {
            *new_op = NC_OP_DELETE;
        }
        else if (g_strcmp0 (attr, "merge") == 0)
        {
            *new_op = NC_OP_MERGE;
        }
        else if (g_strcmp0 (attr, "replace") == 0)
        {
            *new_op = NC_OP_REPLACE;
        }
        else if (g_strcmp0 (attr, "create") == 0)
        {
            *new_op = NC_OP_CREATE;
        }
        else if (g_strcmp0 (attr, "remove") == 0)
        {
            *new_op = NC_OP_REMOVE;
        }
        else
        {
            if (parms != NULL)
            {
                parms->error_tag = "bad-attribute";
            }
            return false;
        }

        /* Check for invvalid transitions between sub-operations. We only allow
         * merge->anything transitions.
         */
        if (curr_op != *new_op && curr_op != NC_OP_MERGE)
        {
            parms->error_tag = "operation-not-supported";
            return false;
        }
    }
    return true;
}

/**
 * Check for existence of data at a particular xpath or below. This is
 * required for NC_OP_CREATE and NC_OP_DELETE. Fill in the error_tag if we don't
 * get expected result, and return existence result.
 *
 */
static bool
_does_exist (const char *check_xpath, edit_config_parameters *parms, bool expected)
{
    GNode *check_result;

    check_result = apteryx_get_tree (check_xpath);

    /* Check existence against expected result - no point if parms not defined */
    if (parms)
    {
        if (check_result && !expected)
        {
            parms->error_tag = "data-exists";
        }
        else if (!check_result && expected)
        {
            parms->error_tag = "data-missing";
        }
    }

    /* Return actual existence of data */
    if (check_result)
    {
        apteryx_free_tree (check_result);
        return true;
    }
    return false;
}

/**
 * Process a node in a config structure.
 * parms - contains a number of input and output parameters
 * schema - parent of this node in the XML data model schema
 * xml - where this node is in the config structure
 * part_xpath - xpath of the current node in the schema
 * curr_op - operation inherited from previous parsing
 * depth - how deep we are into the recursive call tree
 */
GNode *
_config_to_tree_and_delete_paths (edit_config_parameters * parms, sch_node * schema, xmlNode * xml, const char * part_xpath, int curr_op, int depth)
{
    const char *name = (const char *) xml->name;
    xmlNode *child;
    char *attr;
    GNode *tree = NULL;
    GNode *node = NULL;
    char *key = NULL;
    char *new_xpath = NULL;
    int new_op = curr_op;
    bool add_to_delete_list = false;

    /* Find schema node */
    if (!schema)
    {
        schema = sch_lookup (parms->in_instance, name);
    }
    else
    {
        schema = sch_node_child (schema, name);
    }
    if (schema == NULL)
    {
        parms->error_tag = "bad-element";
        return NULL;
    }

    /* Update xpath. */
    new_xpath = g_strdup_printf ("%s/%s", part_xpath, name);

    /* Check operation */
    if (!_operation_ok (xml, curr_op, &new_op, parms))
    {
        free (new_xpath);
        return NULL;
    }

    /* LIST */
    if (sch_is_list (schema))
    {
        char *old_xpath = new_xpath;
        char *key_value;

        key = sch_name (sch_node_child_first (sch_node_child_first (schema)));
        depth++;
        tree = node = APTERYX_NODE (NULL, g_strdup (name));
        attr = (char *) xmlGetProp (xml, BAD_CAST key);
        if (attr)
        {
            node = APTERYX_NODE (node, attr);
            if (!(parms->in_flags & SCH_F_STRIP_KEY) || xmlFirstElementChild (xml))
            {
                APTERYX_NODE (node, g_strdup (key));
            }
            key_value = attr;
        }
        else if (xmlFirstElementChild (xml) &&
                 g_strcmp0 ((const char *) xmlFirstElementChild (xml)->name, key) == 0 &&
                 xml_node_has_content (xmlFirstElementChild (xml)))
        {
            node =
                APTERYX_NODE (node,
                              (char *) xmlNodeGetContent (xmlFirstElementChild (xml)));
            key_value = (char *) xmlNodeGetContent (xmlFirstElementChild (xml));
        }
        else
        {
            node = APTERYX_NODE (node, g_strdup ("*"));
            key_value = "*";
        }
        schema = sch_node_child_first (schema);
        new_xpath = g_strdup_printf ("%s/%s", old_xpath, key_value);
        free (old_xpath);
    }
    /* CONTAINER */
    else if (!sch_is_leaf (schema))
    {
        tree = node = APTERYX_NODE (NULL, g_strdup_printf ("%s%s", depth ? "" : "/", name));
    }
    /* LEAF */
    else
    {
        if (new_op != NC_OP_DELETE && new_op != NC_OP_REMOVE)
        {
            tree = node = APTERYX_NODE (NULL, g_strdup (name));
            if (xml_node_has_content (xml))
            {
                node = APTERYX_NODE (tree, (char *) xmlNodeGetContent (xml));
            }
        }
    }

    /* Delete actions. */
    if (curr_op != NC_OP_DELETE && curr_op != NC_OP_REMOVE &&
        (new_op == NC_OP_DELETE || new_op == NC_OP_REMOVE))
    {
        add_to_delete_list = true;
        if (new_op == NC_OP_DELETE)
        {
            if (!_does_exist (new_xpath, parms, true))
            {
                free (new_xpath);
                return NULL;
            }
        }
    }
    else if (curr_op != NC_OP_REPLACE && new_op == NC_OP_REPLACE)
    {
        add_to_delete_list = true;
    }
    if (add_to_delete_list)
    {
        parms->out_delete_paths = g_list_append (parms->out_delete_paths, g_strdup (new_xpath));
    }

    /* Create actions */
    if (curr_op != NC_OP_CREATE && new_op == NC_OP_CREATE)
    {
        if (_does_exist (new_xpath, parms, false))
        {
            free (new_xpath);
            return NULL;
        }
    }

    for (child = xmlFirstElementChild (xml); child; child = xmlNextElementSibling (child))
    {
        if ((parms->in_flags & SCH_F_STRIP_KEY) && key &&
            g_strcmp0 ((const char *) child->name, key) == 0)
        {
            /* The only child is the key with value */
            if (xmlChildElementCount (xml) == 1)
            {
                if (xml_node_has_content (child))
                {
                    /* Want all parameters for one entry in list. */
                    APTERYX_NODE (node, g_strdup ("*"));
                }
                else
                {
                    /* Want one field in list element for one or more entries */
                    APTERYX_NODE (node, g_strdup ((const char *) child->name));
                }
                break;
            }
            /* Multiple children - make sure key appears */
            else if (xmlChildElementCount (xml) > 1)
            {
                APTERYX_NODE (node, g_strdup ((const char *) child->name));
            }
        }
        else
        {
            GNode *cn = _config_to_tree_and_delete_paths (parms, schema, child, new_xpath, new_op, depth + 1);
            if (cn)
            {
                g_node_append (node, cn);
            }
        }
    }

    /* Get everything from here down if a trunk of a subtree */
    if (!xmlFirstElementChild (xml) && sch_node_child_first (schema) &&
        g_strcmp0 (APTERYX_NAME (node), "*") != 0)
    {
        APTERYX_NODE (node, g_strdup ("*"));
    }

    free (key);
    free (new_xpath);
    return tree;
}

void
config_to_tree_and_delete_paths (edit_config_parameters * parms)
{
    /* Initialise information used to descrned the config data */
    parms->out_set_tree = _config_to_tree_and_delete_paths (parms, NULL, xmlFirstElementChild (parms->in_xml), "", parms->in_def_op, 0);
}

static bool
handle_edit (struct netconf_session *session, xmlNode * rpc)
{
    xmlNode *action = xmlFirstElementChild (rpc);
    xmlNode *node;
    GNode *tree = NULL;
    int schflags = 0;
    edit_config_parameters parms;
    GList *iter;

    if (apteryx_netconf_verbose)
        schflags |= SCH_F_DEBUG;

    /* Check the target */
    node = xmlFindNodeByName (action, BAD_CAST "target");
    if (!node || !xmlFirstElementChild (node) ||
        xmlStrcmp (xmlFirstElementChild (node)->name, BAD_CAST "running"))
    {
        VERBOSE ("Datastore \"%s\" not supported",
                 (char *) xmlFirstElementChild (node)->name);
        return send_rpc_error (session, rpc, "operation-not-supported");
    }

    //TODO Check default-operation
    //TODO Check test-option
    //TODO Check error-option

    /* Find the config */
    node = xmlFindNodeByName (action, BAD_CAST "config");
    if (!node)
    {
        VERBOSE ("Missing \"config\" element");
        return send_rpc_error (session, rpc, "missing-element");
    }

    /* Set up parameters for message parsing. */
    parms.in_def_op = NC_OP_MERGE;
    parms.in_instance = g_schema;
    parms.in_flags = schflags;
    parms.in_xml = node;
    parms.out_set_tree = NULL;
    parms.out_delete_paths = NULL;
    parms.error_tag = NULL;

    config_to_tree_and_delete_paths (&parms);

    tree = parms.out_set_tree;

    //TODO - permissions
    //TODO - patterns

    /* Edit database */
    DEBUG ("NETCONF: SET %s\n", tree ? APTERYX_NAME (tree) : "NULL");
    if (parms.error_tag)
    {
        apteryx_free_tree (tree);
        return send_rpc_error (session, rpc, parms.error_tag);
    }

    /* Anything on the delete list should be purged. */
    for (iter = parms.out_delete_paths; iter; iter = g_list_next (iter))
    {
        char *prune_string = (char *) iter->data;
        iter->data = NULL;
        apteryx_prune (prune_string);
        free (prune_string);
    }

    if (tree && !apteryx_set_tree (tree))
    {
        apteryx_free_tree (tree);
        return send_rpc_error (session, rpc, "operation-failed");
    }
    apteryx_free_tree (tree);

    /* Success */
    return send_rpc_ok (session, rpc);
}

static struct netconf_session *
create_session (int fd)
{
    struct netconf_session *session = g_malloc (sizeof (struct netconf_session));
    session->fd = fd;
    return session;
}

static void
destroy_session (struct netconf_session *session)
{
    close (session->fd);
    g_free (session);
}

/* \n#<chunk-size>\n with max chunk-size = 4294967295 */
#define MAX_CHUNK_HEADER_SIZE 13

static int
read_chunk_size (struct netconf_session *session)
{
    char chunk_header[MAX_CHUNK_HEADER_SIZE + 1];
    int chunk_len = 0;
    char *pt = chunk_header;
    int len = 0;

    /* Read chunk-size (\n#<chunk-size>\n */
    while (g_main_loop_is_running (g_loop))
    {
        if (len > MAX_CHUNK_HEADER_SIZE || recv (session->fd, pt, 1, 0) != 1)
        {
            ERROR ("RX Failed to read chunk header byte\n");
            break;
        }
        pt[1] = '\0';
        if (len >= 3 && chunk_header[0] == '\n' && chunk_header[1] == '#' &&
            chunk_header[len] == '\n')
        {
            if (g_strcmp0 (chunk_header, "\n##\n") == 0)
                break;
            if (sscanf (chunk_header, "\n#%d", &chunk_len) == 1)
            {
                VERBOSE ("RX(%ld): %.*s\n", (pt - chunk_header), (int) (pt - chunk_header),
                         chunk_header);
                break;
            }
        }
        len++;
        pt++;
    }
    return chunk_len;
}

static char *
receive_message (struct netconf_session *session, int *rlen)
{
    char *message = NULL;
    int len = 0;

    /* Read chunks until we get the end of message marker */
    while (g_main_loop_is_running (g_loop))
    {
        int chunk_len;

        /* Get chunk length */
        chunk_len = read_chunk_size (session);
        if (!chunk_len)
        {
            /* End of message */
            break;
        }

        /* Read chunk */
        if (!message)
            message = g_malloc (chunk_len);
        else
            message = g_realloc (message, len + chunk_len);
        if (recv (session->fd, message + len, chunk_len, 0) != chunk_len)
        {
            ERROR ("RX Failed to read %d bytes of chunk\n", chunk_len);
            g_free (message);
            message = NULL;
            len = 0;
            break;
        }
        VERBOSE ("RX(%d):\n%.*s\n", chunk_len, chunk_len, message + len);
        len += chunk_len;
    }

    *rlen = len;
    return message;
}

void *
netconf_handle_session (int fd)
{
    struct netconf_session *session = create_session (fd);

    /* Process hello's first */
    if (!handle_hello (session))
    {
        destroy_session (session);
        return NULL;
    }

    /* Process chunked RPC's */
    while (g_main_loop_is_running (g_loop))
    {
        xmlDoc *doc = NULL;
        xmlNode *rpc, *child;
        char *message;
        int len;

        /* Receive message */
        message = receive_message (session, &len);
        if (!message)
        {
            break;
        }

        /* Parse RPC */
        doc = xmlParseMemory (message, len);
        if (!doc)
        {
            ERROR ("XML: Invalid Netconf message\n");
            g_free (message);
            break;
        }
        rpc = xmlDocGetRootElement (doc);
        if (!rpc || g_strcmp0 ((char *) rpc->name, "rpc") != 0)
        {
            ERROR ("XML: No root RPC element\n");
            xmlFreeDoc (doc);
            g_free (message);
            break;
        }

        /* Process RPC */
        child = xmlFirstElementChild (rpc);
        if (!child)
        {
            ERROR ("XML: No RPC child element\n");
            xmlFreeDoc (doc);
            g_free (message);
            break;;
        }

        if (g_strcmp0 ((char *) child->name, "close-session") == 0)
        {
            VERBOSE ("Closing session\n");
            send_rpc_ok (session, rpc);
            xmlFreeDoc (doc);
            g_free (message);
            break;
        }
        else if (g_strcmp0 ((char *) child->name, "get") == 0)
        {
            VERBOSE ("Handle RPC %s\n", (char *) child->name);
            handle_get (session, rpc, false);
        }
        else if (g_strcmp0 ((char *) child->name, "get-config") == 0)
        {
            VERBOSE ("Handle RPC %s\n", (char *) child->name);
            handle_get (session, rpc, true);
        }
        else if (g_strcmp0 ((char *) child->name, "edit-config") == 0)
        {
            VERBOSE ("Handle RPC %s\n", (char *) child->name);
            handle_edit (session, rpc);
        }
        else
        {
            VERBOSE ("Unknown RPC (%s)\n", child->name);
            send_rpc_error (session, rpc, "operation-not-supported");
            xmlFreeDoc (doc);
            g_free (message);
            break;
        }

        xmlFreeDoc (doc);
        g_free (message);
    }

    VERBOSE ("NETCONF: session terminated\n");
    destroy_session (session);
    return NULL;
}

gboolean
netconf_init (const char *path, const char *cp, const char *rm)
{
    /* Load Data Models */
    g_schema = sch_load (path);
    if (!g_schema)
    {
        return false;
    }

    return true;
}

void
netconf_shutdown (void)
{
    /* Cleanup datamodels */
    if (g_schema)
        sch_free (g_schema);
}
