import os
import pytest
from ncclient import manager
from ncclient.operations import RPCError
from ncclient.xml_ import to_ele
from lxml import etree

# TEST CONFIGURATION

host = 'localhost'
port = 830
username = 'manager'
password = 'friend'

APTERYX = 'LD_LIBRARY_PATH=.build/usr/lib .build/usr/bin/apteryx'

# TEST HELPERS

db_default = [
    ('/test/settings/debug', 'enable'),
    ('/test/settings/enable', 'true'),
    ('/test/settings/priority', '1'),
    ('/test/settings/lastmod', '1567898765'),
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
    for path, value in db_default:
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
    if len(a) != len(b):
        return "len(%s) != len(%s)" % (a.tag, b.tag)
    if a.tag != b.tag:
        return "%s != %s" % (a.tag, b.tag)
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
    assert ":writable-running" in m.server_capabilities
    # assert ":startup" in m.server_capabilities
    assert ":xpath" in m.server_capabilities
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


def _get_test_with_filter(f_value, expected=None, f_type='subtree'):
    """
    Perform a get with the given filter, which can be of type 'subtree' or 'xpath'. If expectede
    respose is given, assert that it was the same as the response from the get. Return the response
    so the caller can perform its own tests.
    """
    m = connect()
    xml = m.get(filter=(f_type, f_value)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    if expected:
        expected = toXML(expected)
        assert diffXML(xml, expected) is None
    m.close_session()
    return xml


def test_get_subtree_no_filter():
    m = connect()
    xml = m.get().data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    # Full tree should be returned
    assert xml.find('./{*}test/{*}settings/{*}debug').text == 'enable'
    assert xml.find('./{*}test/{*}state/{*}counter').text == '42'
    assert xml.find('./{*}test/{*}animals/{*}animal/{*}name').text == 'cat'
    m.close_session()


@pytest.mark.skip(reason="exception creating RPC")
def test_get_subtree_empty_filter():
    m = connect()
    # ncclient does not allow empty filter strings - so make our own rpc
    rpc = """<get><filter type="subtree"></filter></get>"""

    xml = to_ele(m.rpc(to_ele(rpc)).xml)
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    # Nothing but an rpc-reply with an empty data attribute should be returned
    assert len(xml) and len(xml[0]) == 0
    m.close_session()


def test_get_subtree_node():
    select = '<test><settings><debug/></settings></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_trunk():
    select = '<test><settings/></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
            <enable>true</enable>
            <priority>1</priority>
            <lastmod>1567898765</lastmod>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_multi_parameters():
    select = '<test><settings><debug/><priority/></settings></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
            <priority>1</priority>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_list_container():
    select = '<test><animals/></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_list_element():
    select = '<test><animals><animal/></animals></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_list_parameter():
    select = '<test><animals><animal><name/></animal></animals></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_selection_multi():
    select = '<test><animals><animal><name/><type/></animal></animals></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_one_node():
    select = '<test><animals><animal><name>cat</name></animal></animals></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_one_elements():
    select = '<test><animals><animal><name>cat</name><type/></animal></animals></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_multi():
    select = '<test><animals><animal><name>cat</name></animal><animal><name>dog</name></animal></animals></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_attr_named_only():
    select = '<test><animals><animal name="cat"/></animals></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_attr_named_element():
    select = '<test><animals><animal name="mouse"><type/></animal></animals></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>mouse</name>
                <type>little</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_no_key_other_field():
    """
    Don't specify key node in filter. RFC states that we MAY include key in output,
    we are not currently doing this.
    """
    select = '<test><animals><animal><type/></animal></animals></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <type>big</type>
            </animal>
            <animal>
                <type>little</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_no_key_other_field_value():
    """
    RFC states 'Filtering of list content is not supported.' This means that this query will return
    all types.
    """
    select = '<test><animals><animal><type>little</type></animal></animals></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <type>big</type>
            </animal>
            <animal>
                <type>little</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_key_other_field_value():
    """
    This test may not be showing correct behaviour.
    """
    select = '<test><animals><animal><name/><type>little</type></animal></animals></test>'
    expected = """
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
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_select_key_value_other_field_value():
    """
    This test may not be showing correct behaviour.
    """
    select = '<test><animals><animal><name>mouse</name><type>little</type></animal></animals></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <animals>
            <animal>
                <name>mouse</name>
                <type>little</type>
            </animal>
        </animals>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_missing():
    select = '<test><animals><animal><name>elephant</name></animal></animals></test>'
    xml = _get_test_with_filter(select)
    assert xml.tag == '{urn:ietf:params:xml:ns:netconf:base:1.0}data'
    assert len(xml.getchildren()) == 0
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))

# TODO :with-defaults
# TODO explicit namespace queries

# GET XPATH


def test_get_xpath_node():
    xpath = '/test/settings/debug'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
        </settings>
    </test>
</nc:data>
    """
    xml = _get_test_with_filter(xpath, expected, f_type='xpath')
    assert xml.find('./{*}test/{*}settings/{*}debug').text == 'enable'


def test_get_xpath_trunk():
    xpath = '/test/settings'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
            <enable>true</enable>
            <priority>1</priority>
            <lastmod>1567898765</lastmod>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(xpath, expected, f_type='xpath')


def test_get_xpath_list_trunk():
    xpath = '/test/animals'
    expected = """
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
    """
    _get_test_with_filter(xpath, expected, f_type='xpath')


def test_get_xpath_list_select_one_trunk():
    xpath = "/test/animals/animal[name='cat']"
    expected = """
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
    """
    _get_test_with_filter(xpath, expected, f_type='xpath')


def test_get_xpath_list_select_one_parameter():
    xpath = "/test/animals/animal[name='cat']/type"
    expected = """
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
    """
    _get_test_with_filter(xpath, expected, f_type='xpath')


@pytest.mark.skip(reason="does not work yet")
def test_xpath_query_multi():
    xpath = ("/test/animals/animal[name='cat']/name | /test/animals/animal[name='dog']/name")
    expected = """
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
    """
    _get_test_with_filter(xpath, expected, f_type='xpath')

# GET-CONFIG


def test_get_config_simple_node():
    m = connect()
    xml = m.get_config(source='running', filter=('xpath', "/test/settings/debug")).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert xml.find('./{*}test/{*}settings/{*}debug').text == 'enable'
    m.close_session()


def test_get_config_unsupported_datastore():
    m = connect()
    response = None
    try:
        response = m.get_config(source='candidate', filter=('xpath', "/test/settings/debug"))
    except RPCError as err:
        print(err)
        assert err.tag == 'operation-not-supported'
    assert response is None, 'Should have received an RPCError'
    m.close_session()


def test_get_config_no_state():
    m = connect()
    xml = m.get_config(source='running').data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    # Full tree should be returned with config only
    assert xml.find('./{*}test/{*}settings/{*}debug').text == 'enable'
    assert xml.find('./{*}test/{*}settings/{*}lastmod') is None
    assert xml.find('./{*}test/{*}state/{*}counter') is None
    assert xml.find('./{*}test/{*}animals/{*}animal/{*}name').text == 'cat'
    # Ignore the rest!
    m.close_session()

# EDIT-CONFIG


def _edit_config_test(payload, expect_err=None, post_xpath=None):
    """
    Run and edit-config with the given payload, optionally checking for error, and
    returning the response from a get carried out with a given xpath.
    """
    m = connect()
    xml = None
    try:
        response = m.edit_config(target='running', config=payload)
        print(response)
    except RPCError as err:
        print(err)
        assert expect_err is not None
        assert err.tag == expect_err
    else:
        assert expect_err is None
        if post_xpath is not None:
            xml = m.get(filter=('xpath', post_xpath)).data
    m.close_session()
    return xml


def test_edit_config_node():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority>99</priority>
    </settings>
  </test>
</config>
"""
    xml = _edit_config_test(payload, post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '99'


def test_edit_config_multi():
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
    xml = _edit_config_test(payload, post_xpath='/test/settings')
    assert xml.find('./{*}test/{*}settings/{*}enable').text == 'false'
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '99'


def test_edit_config_list():
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
    xml = _edit_config_test(payload, post_xpath='/test/animals')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert 'frog' in etree.XPath("//text()")(xml)


# EDIT-CONFIG (operation="delete")

def test_edit_config_delete_invalid_path():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <missing operation="delete">1</missing>
    </settings>
  </test>
</config>
"""
    _edit_config_test(payload, expect_err='malformed-message')


def test_edit_config_delete_node():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="delete">1</priority>
    </settings>
  </test>
</config>
"""
    xml = _edit_config_test(payload, post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority') is None


@pytest.mark.skip(reason="does not work - we return success even if there is no data")
def test_edit_config_delete_no_data():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="delete">1</priority>
    </settings>
  </test>
</config>
"""
    _edit_config_test(payload, expect_err='data-missing')


def test_edit_config_delete_multi():
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
    xml = _edit_config_test(payload, post_xpath='/test/settings')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert etree.XPath("//text()")(xml) == ['enable', '1567898765']


@pytest.mark.skip(reason="does not work yet")
def test_edit_config_delete_trunk():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings xc:operation="delete" />
  </test>
</config>
"""
    xml = _edit_config_test(payload, post_xpath='/test/settings')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert etree.XPath("//text()")(xml) == []


@pytest.mark.skip(reason="does not work yet")
def test_edit_config_delete_list():
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
    xml = _edit_config_test(payload, post_xpath='/test/animals')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert 'cat' in etree.XPath("//text()")(xml)


def test_edit_config_merge_delete():
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
    xml = _edit_config_test(payload, post_xpath='/test/settings')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert etree.XPath("//text()")(xml) == ['enable', 'false', '1567898765']


# TODO EDIT-CONFIG (operation:default=merge)
    #  replace:  The configuration data identified by the element
    #     containing this attribute replaces any related configuration
    #     in the configuration datastore identified by the <target>
    #     parameter.  If no such configuration data exists in the
    #     configuration datastore, it is created.  Unlike a
    #     <copy-config> operation, which replaces the entire target
    #     configuration, only the configuration actually present in
    #     the <config> parameter is affected.

@pytest.mark.skip(reason="does not work")
def test_edit_config_replace_all():
    """
    Replace all animals with one (existing) animal.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals xc:operation="replace">
      <animal>
        <name>cat</name>
        <type>big</type>
        <colour>tawny</colour>
      </animal>
    </animals>
  </test>
</config>
"""
    xml = _edit_config_test(payload, post_xpath='/test/animals')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert 'cat' in etree.XPath("//text()")(xml)
    assert 'dog' not in etree.XPath("//text()")(xml)
    assert 'mouse' not in etree.XPath("//text()")(xml)


def test_edit_config_replace_one_full():
    """
    Replace one animal. Fully specify the replacement.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
      <animal xc:operation="replace">
        <name>cat</name>
        <type>little</type>
        <colour>tawny</colour>
      </animal>
    </animals>
  </test>
</config>
"""
    xml = _edit_config_test(payload, post_xpath='/test/animals')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert 'cat' in etree.XPath("//text()")(xml)
    assert 'dog' in etree.XPath("//text()")(xml)
    assert 'mouse' in etree.XPath("//text()")(xml)
    assert xml.find('./{*}test/{*}animals/{*}animal[name="cat"]/{*}type').text == 'little'
    assert xml.find('./{*}test/{*}animals/{*}animal[name="cat"]/{*}colour').text == 'tawny'


@pytest.mark.skip(reason="does not work")
def test_edit_config_replace_one_default():
    """
    Replace one animal. Allow all values to revert to default.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
      <animal xc:operation="replace">
        <name>mouse</name>
      </animal>
    </animals>
  </test>
</config>
"""
    xml = _edit_config_test(payload, post_xpath='/test/animals')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert 'cat' in etree.XPath("//text()")(xml)
    assert 'dog' in etree.XPath("//text()")(xml)
    assert 'mouse' in etree.XPath("//text()")(xml)
    assert xml.find('./{*}test/{*}animals/{*}animal[name="mouse"]/{*}type').text == 'big'
    assert xml.find('./{*}test/{*}animals/{*}animal[name="mouse"]/{*}colour') is None

    #  create:  The configuration data identified by the element
    #     containing this attribute is added to the configuration if
    #     and only if the configuration data does not already exist in
    #     the configuration datastore.  If the configuration data
    #     exists, an <rpc-error> element is returned with an
    #     <error-tag> value of "data-exists".


def test_edit_config_create_new():
    """
    Create one animal. It does not already exist.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
      <animal xc:operation="create">
        <name>dolphin</name>
        <type>big</type>
      </animal>
    </animals>
  </test>
</config>
"""
    xml = _edit_config_test(payload, post_xpath='/test/animals')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert 'dolphin' in etree.XPath("//text()")(xml)
    assert xml.find('./{*}test/{*}animals/{*}animal[name="dolphin"]/{*}type').text == 'big'
    assert xml.find('./{*}test/{*}animals/{*}animal[name="dolphin"]/{*}colour') is None


@pytest.mark.skip(reason="does not work")
def test_edit_config_create_exists():
    """
    Create one animal. It already exists.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
      <animal xc:operation="create">
        <name>mouse</name>
        <type>little</type>
      </animal>
    </animals>
  </test>
</config>
"""
    _edit_config_test(payload, expect_err='data-exists')

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
