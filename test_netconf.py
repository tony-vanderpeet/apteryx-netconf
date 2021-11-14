import os
import pytest
from ncclient import manager
from ncclient.xml_ import to_ele
from lxml import etree

# TEST CONFIGURATION

host='localhost'
port=830
username='manager'
password='friend'

APTERYX='LD_LIBRARY_PATH=.build/usr/lib .build/usr/bin/apteryx'

# TEST HELPERS

db_default = [
    ('/test/settings/debug', 'enable'),
    ('/test/settings/enable', 'true'),
    ('/test/settings/priority', '1'),
    ('/test/state/counter', '42'),
    ('/test/animals/animal/cat/name', 'cat'),
    ('/test/animals/animal/cat/type', 'big'),
    ('/test/animals/animal/dog/name', 'dog'),
    ('/test/animals/animal/dog/colour', 'brown'),
    ('/test/animals/animal/mouse/name', 'mouse'),
    ('/test/animals/animal/mouse/type', 'little'),
    ('/test/animals/animal/mouse/colour', 'grey'),
]

@pytest.fixture(autouse=True)
def run_around_tests():
    # Before test
    os.system("echo Before test")
    os.system("%s -r /test" % (APTERYX))
    for path,value in db_default:
        os.system("%s -s %s %s" % (APTERYX, path, value))
    yield
    # After test
    os.system("echo After test")
    os.system("%s -r /test" % (APTERYX))

def connect():
    return manager.connect(host=host,
                    port=port,
                    username=username,
                    password=password,
                    hostkey_verify=False,
                    allow_agent=False,
                    look_for_keys=False)

def toXML(xml_str):
    parser = etree.XMLParser(remove_blank_text=True)
    return etree.XML(xml_str, parser=parser)

def diffXML(a, b):
    if len(a) != len(b): return "len(%s) != len(%s)" % (a.tag, b.tag)
    if a.tag != b.tag: return "%s != %s" % (a.tag, b.tag)
    if a is not None and len(a):
        for ae, be in zip(a, b):
            cmp = diffXML(ae, be)
            if cmp is not None:
                return cmp
    if a.text is None and b.text is not None:
        return "a.text is None and b.text is not None"
    elif a.text is not None and b.text is None:
        return "a.text is not None and b.text is None"
    elif a.text and a.text.strip() != b.text.strip():
        return "%s == %s" % (a.text.strip(), b.text.strip())
    return None

# CAPABILITIES

def test_server_capabilities():
    m = connect()
    for capability in m.server_capabilities:
        print("Capability: %s" % capability.split('?')[0])
    assert ":base" in m.server_capabilities
    # assert ":writable-running" in m.server_capabilities
    # assert ":startup" in m.server_capabilities
    # assert ":xpath" in m.server_capabilities
    assert ":with-defaults" in m.server_capabilities

    assert ":candidate" not in m.server_capabilities
    assert ":rollback-on-error" not in m.server_capabilities
    assert ":url" not in m.server_capabilities
    assert ":confirmed-commit" not in m.server_capabilities
    assert ":validate" not in m.server_capabilities
    assert ":power-control" not in m.server_capabilities
    assert ":notification" not in m.server_capabilities
    assert ":interleave" not in m.server_capabilities
    m.close_session()

# GET SUBTREE

@pytest.mark.skip(reason="does not work yet - get_tree does not let you grab the root of the world!")
def test_get_subtree_no_filter():
    m = connect()
    xml = m.get().data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    # Full tree should be returned
    assert xml.find('./{*}test/{*}debug').text == 'enable'
    m.close_session()

@pytest.mark.skip(reason="does not work yet - ncclient does not allow empty filter strings")
def test_get_subtree_empty_filter():
    m = connect()
    xml = m.get(filter=('subtree', "")).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    # Nothing should be returned
    assert xml.find('./{*}test/{*}debug').text != 'enable'
    m.close_session()

def test_get_subtree_node():
    select = '<test><settings><debug/></settings></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
        </settings>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_trunk():
    select = '<test><settings/></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
            <enable>true</enable>
            <priority>1</priority>
        </settings>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_multi_parameters():
    select = '<test><settings><debug/><priority/></settings></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
            <priority>1</priority>
        </settings>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_list_container():
    select = '<test><animals/></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
            <animal>
                <name>dog</name>
                <colour>brown</colour>
            </animal>
            <animal>
                <name>mouse</name>
                <type>little</type>
                <colour>grey</colour>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

@pytest.mark.skip(reason="does not work - rfc6241:6.4.3 - broken libnetconf2?")
def test_get_subtree_list_element():
    select = '<test><animals><animal/></animals></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
            <animal>
                <name>dog</name>
                <colour>brown</colour>
            </animal>
            <animal>
                <name>mouse</name>
                <type>little</type>
                <colour>grey</colour>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_list_parameter():
    select = '<test><animals><animal><name/></animal></animals></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
            </animal>
            <animal>
                <name>dog</name>
            </animal>
            <animal>
                <name>mouse</name>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_selection_multi():
    select = '<test><animals><animal><name/><type/></animal></animals></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
            <animal>
                <name>dog</name>
            </animal>
            <animal>
                <name>mouse</name>
                <type>little</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_select_one_node():
    select = '<test><animals><animal><name>cat</name></animal></animals></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_select_one_elements():
    select = '<test><animals><animal><name>cat</name><type/></animal></animals></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(xml)
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_select_multi():
    select = '<test><animals><animal><name>cat</name></animal><animal><name>dog</name></animal></animals></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(xml)
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
            <animal>
                <name>dog</name>
                <colour>brown</colour>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

@pytest.mark.skip(reason="does not work - notsure how to parse attributes in libnetconf2?")
def test_get_subtree_select_attr_named():
    select = '<test><animals><animal name="cat"/></animals></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    print(xml)
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(xml, expected) == None
    m.close_session()

def test_get_subtree_unknown():
    select = '<test><unknown/></test>'
    m = connect()
    xml = m.get(filter=('subtree', select)).data
    assert xml.tag == '{urn:ietf:params:xml:ns:netconf:base:1.0}data'
    assert len(xml.getchildren()) == 0
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    m.close_session()

# TODO :with-defaults
# TODO explicit namespace queries

# GET XPATH

def test_get_xpath_node():
    m = connect()
    xml = m.get(filter=('xpath', '/test/settings/debug')).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert xml.find('./{*}test/{*}settings/{*}debug').text == 'enable'
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
        </settings>
    </test>
</nc:data>
    """)
    assert diffXML(expected, xml) == None
    m.close_session()

def test_get_xpath_trunk():
    m = connect()
    xml = m.get(filter=('xpath', '/test/settings')).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
            <enable>true</enable>
            <priority>1</priority>
        </settings>
    </test>
</nc:data>
    """)
    assert diffXML(expected, xml) == None
    m.close_session()

def test_get_xpath_list_trunk():
    m = connect()
    xml = m.get(filter=('xpath', '/test/animals')).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
            <animal>
                <name>dog</name>
                <colour>brown</colour>
            </animal>
            <animal>
                <name>mouse</name>
                <type>little</type>
                <colour>grey</colour>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(expected, xml) == None
    m.close_session()

# @pytest.mark.skip(reason="does not work yet")
def test_get_xpath_list_select_one_trunk():
    xpath = "/test/animals/animal[name='cat']"
    m = connect()
    xml = m.get(filter=('xpath', xpath)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(expected, xml) == None
    m.close_session()

def test_get_xpath_list_select_one_parameter():
    xpath = "/test/animals/animal[name='cat']/type"
    m = connect()
    xml = m.get(filter=('xpath', xpath)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
                <type>big</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(expected, xml) == None
    m.close_session()

@pytest.mark.skip(reason="does not work yet")
def test_xpath_query_multi():
    xpath = ("/test/animals/animal[name='cat']/name | /test/animals/animal[name='dog']/name")
    m = connect()
    xml = m.get(filter=('xpath', xpath)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    expected = toXML("""
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>cat</name>
            </animal>
            <animal>
                <name>dog</name>
            </animal>
        </animals>
    </test>
</nc:data>
    """)
    assert diffXML(expected, xml) == None
    m.close_session()

# GET-CONFIG

@pytest.mark.skip(reason="does not work yet")
def test_get_config():
    m = connect()
    xml = m.get_config(source='running').data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert xml.find('./{*}test/{*}settings/{*}debug').text == 'enable'
    # Ignore the rest!
    m.close_session()


# EDIT-CONFIG

def test_edit_config_node():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority>99</priority>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    assert m.get(filter=('xpath', '/test/settings/priority')).data.find('./{*}test/{*}settings/{*}priority').text == '99'
    m.close_session()

def test_edit_config_multi():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <enable>false</enable>
        <priority>99</priority>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    assert m.get(filter=('xpath', '/test/settings/enable')).data.find('./{*}test/{*}settings/{*}enable').text == 'false'
    assert m.get(filter=('xpath', '/test/settings/priority')).data.find('./{*}test/{*}settings/{*}priority').text == '99'
    m.close_session()

def test_edit_config_list():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
        <animal>
            <name>frog</name>
            <type>little</type>
        </animal>
    </animals>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    xml = m.get(filter=('xpath', '/test/animals')).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert 'frog' in etree.XPath("//text()")(xml)
    m.close_session()

# EDIT-CONFIG (operation="delete")

@pytest.mark.skip(reason="does not work yet")
def test_edit_config_delete_invalid_path():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <missing operation="delete">1</missing>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    assert 'data-missing' in response.xml
    m.close_session()

def test_edit_config_delete_node():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="delete">1</priority>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    assert m.get(filter=('xpath', '/test/settings/priority')).data.find('./{*}test/{*}settings/{*}priority') == None
    m.close_session()

@pytest.mark.skip(reason="does not work - we return success even if there is no data")
def test_edit_config_delete_no_data():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="delete">1</priority>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    response = m.edit_config(target='running', config=payload)
    print(response)
    assert 'data-missing' in response.xml
    m.close_session()

def test_edit_config_delete_multi():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <enable xc:operation="delete">true</enable>
        <priority xc:operation="delete">1</priority>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    xml = m.get(filter=('xpath', '/test/settings')).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert etree.XPath("//text()")(xml) == ['enable']
    m.close_session()

@pytest.mark.skip(reason="does not work yet")
def test_edit_config_delete_trunk():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings xc:operation="delete" />
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    xml = m.get(filter=('xpath', '/test/settings')).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert etree.XPath("//text()")(xml) == []
    m.close_session()

@pytest.mark.skip(reason="does not work yet")
def test_edit_config_delete_list():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
        <animal xc:operation="delete">
            <name>cat</name>
            <type>big</type>
        </animal>
    </animals>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    xml = m.get(filter=('xpath', '/test/animals')).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert 'cat' in etree.XPath("//text()")(xml)
    m.close_session()

def test_edit_config_merge_delete():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <enable>false</enable>
        <priority xc:operation="delete">1</priority>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    xml = m.get(filter=('xpath', '/test/settings')).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert etree.XPath("//text()")(xml) == ['enable', 'false']
    m.close_session()

# TODO EDIT-CONFIG (operation:default=merge)
        #  replace:  The configuration data identified by the element
        #     containing this attribute replaces any related configuration
        #     in the configuration datastore identified by the <target>
        #     parameter.  If no such configuration data exists in the
        #     configuration datastore, it is created.  Unlike a
        #     <copy-config> operation, which replaces the entire target
        #     configuration, only the configuration actually present in
        #     the <config> parameter is affected.

        #  create:  The configuration data identified by the element
        #     containing this attribute is added to the configuration if
        #     and only if the configuration data does not already exist in
        #     the configuration datastore.  If the configuration data
        #     exists, an <rpc-error> element is returned with an
        #     <error-tag> value of "data-exists".

        #  remove:  The configuration data identified by the element
        #     containing this attribute is deleted from the configuration
        #     if the configuration data currently exists in the
        #     configuration datastore.  If the configuration data does not
        #     exist, the "remove" operation is silently ignored by the
        #     server.

# TODO VALIDATE
# TODO COPY-CONFIG
# TODO DELETE-CONFIG
# TODO LOCK/UNLOCK

