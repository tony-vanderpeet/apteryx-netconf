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
    # Default namespace
    ('/test/settings/debug', '1'),
    ('/test/settings/enable', 'true'),
    ('/test/settings/priority', '1'),
    ('/test/settings/hidden', 'friend'),
    ('/test/state/counter', '42'),
    ('/test/state/uptime/days', '5'),
    ('/test/state/uptime/hours', '50'),
    ('/test/state/uptime/minutes', '30'),
    ('/test/state/uptime/seconds', '20'),
    ('/test/animals/animal/cat/name', 'cat'),
    ('/test/animals/animal/cat/type', '1'),
    ('/test/animals/animal/dog/name', 'dog'),
    ('/test/animals/animal/dog/colour', 'brown'),
    ('/test/animals/animal/mouse/name', 'mouse'),
    ('/test/animals/animal/mouse/type', '2'),
    ('/test/animals/animal/mouse/colour', 'grey'),
    ('/test/animals/animal/hamster/name', 'hamster'),
    ('/test/animals/animal/hamster/type', '2'),
    ('/test/animals/animal/hamster/food/banana/name', 'banana'),
    ('/test/animals/animal/hamster/food/banana/type', 'fruit'),
    ('/test/animals/animal/hamster/food/nuts/name', 'nuts'),
    ('/test/animals/animal/hamster/food/nuts/type', 'kibble'),
    ('/test/animals/animal/parrot/name', 'parrot'),
    ('/test/animals/animal/parrot/type', '1'),
    ('/test/animals/animal/parrot/colour', 'blue'),
    ('/test/animals/animal/parrot/toys/toy/rings', 'rings'),
    ('/test/animals/animal/parrot/toys/toy/puzzles', 'puzzles'),
    # Default namespace augmented path
    ('/test/settings/volume', '1'),
    # Non-default namespace same path as default
    ('/t2:test/settings/priority', '2'),
    # Non-default namespace augmented path
    ('/t2:test/settings/speed', '2'),
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
    os.system("%s -r /t2:test" % (APTERYX))


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
        print("Capability: %s" % capability)
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
    # Supported models - first is default namespace
    assert "https://github.com/alliedtelesis/apteryx?module=testing&revision=2023-01-01" in m.server_capabilities
    assert "http://test.com/ns/yang/testing-2?module=testing-2&revision=2023-02-01" in m.server_capabilities
    assert "http://test.com/ns/yang/testing2-augmented?module=testing2-augmented&revision=2023-02-02" in m.server_capabilities
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


# No default or prefixed namespace - we use the internal default namespace
def test_get_subtree_node_ns_none():
    select = '<test><settings><priority/></settings></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <priority>1</priority>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_node_ns_aug_none():
    select = '<test><settings><volume/></settings></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <volume>1</volume>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


# Default namespace is the same as the internal default namespace
def test_get_subtree_node_ns_default():
    select = '<test xmlns="https://github.com/alliedtelesis/apteryx"><settings><priority/></settings></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <priority>1</priority>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_node_ns_aug_default():
    select = '<test xmlns="https://github.com/alliedtelesis/apteryx"><settings><volume/></settings></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <volume>1</volume>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


# Default namespace is not the internal default namespace
def test_get_subtree_node_ns_other_no_prefix():
    select = '<test xmlns="http://test.com/ns/yang/testing-2"><settings><priority/></settings></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <priority>2</priority>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_node_ns_aug_other_no_prefix():
    select = '<test xmlns="http://test.com/ns/yang/testing-2"><settings><speed xmlns="http://test.com/ns/yang/testing2-augmented"/></settings></test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <speed>2</speed>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


# Prefixed namespace is not the internal default namespace
# Note that prefixes are not inherited, so must be specified on every node

def test_get_subtree_node_ns_other_prefix():
    select = '<t2:test xmlns:t2="http://test.com/ns/yang/testing-2"><t2:settings><t2:priority/></t2:settings></t2:test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <priority>2</priority>
        </settings>
    </test>
</nc:data>
    """
    _get_test_with_filter(select, expected)


def test_get_subtree_node_ns_aug_other_prefix():
    select = '<t2:test xmlns:t2="http://test.com/ns/yang/testing-2" xmlns:aug2="http://test.com/ns/yang/testing2-augmented"><t2:settings><aug2:speed/></t2:settings></t2:test>'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <speed>2</speed>
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
            <volume>1</volume>
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
        <name>hamster</name>
        <type>little</type>
        <food>
          <name>banana</name>
          <type>fruit</type>
        </food>
        <food>
          <name>nuts</name>
          <type>kibble</type>
        </food>
      </animal>
      <animal>
        <name>mouse</name>
        <type>little</type>
        <colour>grey</colour>
      </animal>
      <animal>
        <name>parrot</name>
        <type>big</type>
        <colour>blue</colour>
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
        <name>hamster</name>
        <type>little</type>
        <food>
          <name>banana</name>
          <type>fruit</type>
        </food>
        <food>
          <name>nuts</name>
          <type>kibble</type>
        </food>
      </animal>
      <animal>
        <name>mouse</name>
        <type>little</type>
        <colour>grey</colour>
      </animal>
      <animal>
        <name>parrot</name>
        <type>big</type>
        <colour>blue</colour>
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
        <name>hamster</name>
      </animal>
      <animal>
        <name>mouse</name>
      </animal>
      <animal>
        <name>parrot</name>
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
        <name>hamster</name>
        <type>little</type>
      </animal>
      <animal>
        <name>mouse</name>
        <type>little</type>
      </animal>
      <animal>
        <name>parrot</name>
        <type>big</type>
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
    select = """
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
    """
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
    we are not currentl doing this.
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
            <animal>
                <type>little</type>
            </animal>
            <animal>
                <type>big</type>
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
            <animal>
                <type>little</type>
            </animal>
            <animal>
                <type>big</type>
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
                <name>hamster</name>
                <type>little</type>
            </animal>
            <animal>
                <name>mouse</name>
                <type>little</type>
            </animal>
            <animal>
                <name>parrot</name>
                <type>big</type>
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
    select = """
<test>
    <animals>
        <animal>
            <name>mouse</name>
            <type>little</type>
        </animal>
    </animals>
</test>
    """
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

# No default or prefixed namespace - we use the internal default namespace
def test_get_xpath_node_ns_none():
    xpath = '/test/settings/priority'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <priority>1</priority>
        </settings>
    </test>
</nc:data>
    """
    xml = _get_test_with_filter(xpath, expected, f_type='xpath')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '1'

def test_get_xpath_node_ns_aug_none():
    xpath = '/test/settings/volume'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <volume>1</volume>
        </settings>
    </test>
</nc:data>
    """
    xml = _get_test_with_filter(xpath, expected, f_type='xpath')
    assert xml.find('./{*}test/{*}settings/{*}volume').text == '1'

# Default namespace is the same as the internal default namespace
@pytest.mark.skip(reason="does not work yet")
def test_get_xpath_node_ns_default():
    xpath = '/test:test/settings/priority'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <priority>1</priority>
        </settings>
    </test>
</nc:data>
    """
    xml = _get_test_with_filter(xpath, expected, f_type='xpath')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '1'

@pytest.mark.skip(reason="does not work yet")
def test_get_xpath_node_ns_aug_default():
    xpath = '/test:test/settings/volume'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <volume>1</volume>
        </settings>
    </test>
</nc:data>
    """
    xml = _get_test_with_filter(xpath, expected, f_type='xpath')
    assert xml.find('./{*}test/{*}settings/{*}volume').text == '1'

# Prefixed namespace is not the internal default namespace
def test_get_xpath_node_ns_other():
    xpath = '/t2:test/t2:settings/t2:priority'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <priority>2</priority>
        </settings>
    </test>
</nc:data>
    """
    xml = _get_test_with_filter(xpath, expected, f_type='xpath')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '2'

def test_get_xpath_node_ns_aug_other():
    xpath = '/t2:test/t2:settings/aug2:speed'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <speed>2</speed>
        </settings>
    </test>
</nc:data>
    """
    xml = _get_test_with_filter(xpath, expected, f_type='xpath')
    assert xml.find('./{*}test/{*}settings/{*}speed').text == '2'

def test_get_xpath_trunk():
    xpath = '/test/settings'
    expected = """
<nc:data xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0">
    <test>
        <settings>
            <debug>enable</debug>
            <enable>true</enable>
            <priority>1</priority>
            <volume>1</volume>
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
        <name>hamster</name>
        <type>little</type>
        <food>
          <name>banana</name>
          <type>fruit</type>
        </food>
        <food>
          <name>nuts</name>
          <type>kibble</type>
        </food>
      </animal>
      <animal>
        <name>mouse</name>
        <type>little</type>
        <colour>grey</colour>
      </animal>
      <animal>
        <name>parrot</name>
        <type>big</type>
        <colour>blue</colour>
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
    assert xml.find('./{*}test/{*}state/{*}counter') is None
    assert xml.find('./{*}test/{*}state/{*}uptime/days') is None
    assert xml.find('./{*}test/{*}animals/{*}animal/{*}name').text == 'cat'
    # Ignore the rest!
    m.close_session()

# EDIT-CONFIG


def _edit_config_test_no_error(payload, get_xpath, in_string=[], out_string=[]):
    """
    Run an edit config test which is not expected to return an error. Check the
    output returned from the specified get.
    """
    m = connect()
    response = m.edit_config(target='running', config=payload)
    print(response)
    xml = m.get(filter=('xpath', get_xpath)).data
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    for ins in in_string:
        assert ins in etree.XPath("//text()")(xml)
    for outs in out_string:
        assert outs not in etree.XPath("//text()")(xml)
    m.close_session()


def _edit_config_test_error(payload, error_tag):
    """
    Run an edit config test which is expected to return an error. Check the
    error code returned.
    """
    m = connect()
    try:
        response = m.edit_config(target='running', config=payload)
    except RPCError as err:
        assert err.tag == error_tag
    else:
        print(response)
        assert False
    finally:
        m.close_session()


def test_edit_config_node():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority>99</priority>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    check_edit = m.get(filter=('xpath', '/test/settings/priority'))
    assert check_edit.data.find('./{*}test/{*}settings/{*}priority').text == '99'
    m.close_session()


def test_edit_config_multi():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
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
    check_edit = m.get(filter=('xpath', '/test/settings/enable'))
    assert check_edit.data.find('./{*}test/{*}settings/{*}enable').text == 'false'
    check_edit = m.get(filter=('xpath', '/test/settings/priority'))
    assert check_edit.data.find('./{*}test/{*}settings/{*}priority').text == '99'
    m.close_session()


def test_edit_config_list():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
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
    _edit_config_test_no_error(payload, "/test/animals", in_string=["frog"])

# EDIT-CONFIG (operation="delete")


def test_edit_config_delete_invalid_path():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <missing operation="delete">1</missing>
    </settings>
  </test>
</config>
"""
    _edit_config_test_error(payload, "malformed-message")


def test_edit_config_delete_node():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="delete">1</priority>
    </settings>
  </test>
</config>
"""
    response = m.edit_config(target='running', config=payload)
    print(response)
    filt = ('xpath', '/test/settings/priority')
    assert m.get(filter=filt).data.find('./{*}test/{*}settings/{*}priority') is None
    m.close_session()


@pytest.mark.skip(reason="does not work - we return success even if there is no data")
def test_edit_config_delete_no_data():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="delete">1</priority>
    </settings>
  </test>
</config>
"""
    _edit_config_test_error(payload, "data-missing")


def test_edit_config_delete_multi():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
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
    assert etree.XPath("//text()")(xml) == ['enable', '1']
    m.close_session()


@pytest.mark.skip(reason="does not work yet")
def test_edit_config_delete_trunk():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
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
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
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
    _edit_config_test_no_error(payload, "/test/animals", in_string=["cat"])


def test_edit_config_merge_delete():
    m = connect()
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
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
    assert etree.XPath("//text()")(xml) == ['enable', 'false', '1']
    m.close_session()


# EDIT-CONFIG (operation=replace)
#  replace:  The configuration data identified by the element
#     containing this attribute replaces any related configuration
#     in the configuration datastore identified by the <target>
#     parameter.  If no such configuration data exists in the
#     configuration datastore, it is created.  Unlike a
#     <copy-config> operation, which replaces the entire target
#     configuration, only the configuration actually present in
#     the <config> parameter is affected.

@pytest.mark.skip(reason="doesn't replace entire animal element, type remains")
def test_edit_config_replace_list_item():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
        <animal xc:operation="replace">
            <name>cat</name>
            <colour>brown</colour>
        </animal>
    </animals>
  </test>
</config>
"""
    _edit_config_test_no_error(payload, "/test/animals", in_string=["brown"], out_string=["big"])


# EDIT-CONFIG (operation=create)
#  create:  The configuration data identified by the element
#     containing this attribute is added to the configuration if
#     and only if the configuration data does not already exist in
#     the configuration datastore.  If the configuration data
#     exists, an <rpc-error> element is returned with an
#     <error-tag> value of "data-exists".

def test_edit_config_create_list_item():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
        <animal xc:operation="create">
            <name>penguin</name>
            <type>big</type>
        </animal>
    </animals>
  </test>
</config>
"""
    _edit_config_test_no_error(payload, "/test/animals", in_string=["penguin"])


@pytest.mark.skip(reason="does not work yet")
def test_edit_config_create_list_item_exists():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
        <animal xc:operation="create">
            <name>cat</name>
            <type>little</type>
        </animal>
    </animals>
  </test>
</config>
"""
    _edit_config_test_error(payload, "data-exists")


def test_edit_config_create_list_item_field():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
        <animal>
            <name>cat</name>
            <colour xc:operation="create">white</colour>
        </animal>
    </animals>
  </test>
</config>
"""
    _edit_config_test_no_error(payload, "/test/animals", in_string=["white"])


@pytest.mark.skip(reason="does not work yet")
def test_edit_config_create_list_item_field_exists():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
        <animal>
            <name>cat</name>
            <type xc:operation="create">little</type>
        </animal>
    </animals>
  </test>
</config>
"""
    _edit_config_test_error(payload, "data-exists")


# EDIT-CONFIG (operation=remove)
#  remove:  The configuration data identified by the element
#     containing this attribute is deleted from the configuration
#     if the configuration data currently exists in the
#     configuration datastore.  If the configuration data does not
#     exist, the "remove" operation is silently ignored by the
#     server.

def test_edit_config_remove_invalid_path():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <missing xc:operation="remove">1</missing>
    </settings>
  </test>
</config>
"""
    _edit_config_test_error(payload, "malformed-message")


def test_edit_config_remove_missing_data():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <animals>
      <animal>
        <dog>
          <colour xc:operation="remove">brown</colour>
        </dog>
      </animal>
    </animals>
  </test>
</config>
"""
    _edit_config_test_error(payload, "malformed-message")

# TODO VALIDATE
# TODO COPY-CONFIG
# TODO DELETE-CONFIG
# TODO LOCK/UNLOCK
