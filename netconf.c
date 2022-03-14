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

#define NETCONF_BASE_1_0_END "]]>]]>"
#define NETCONF_BASE_1_1_END "\n##\n"

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
handle_edit (struct netconf_session *session, xmlNode * rpc)
{
    xmlNode *action = xmlFirstElementChild (rpc);
    xmlNode *node;
    GNode *tree = NULL;
    int schflags = 0;

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

    /* Convert to gnode */
    tree = sch_xml_to_gnode (g_schema, NULL, xmlFirstElementChild (node), schflags);
    if (!tree)
    {
        VERBOSE ("SETTREE: malformed tree\n");
        return send_rpc_error (session, rpc, "malformed-message");
    }

    //TODO - permissions
    //TODO - patterns

    /* Edit database */
    DEBUG ("NETCONF: SET %s\n", tree ? APTERYX_NAME (tree) : "NULL");
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
