import pytest
from ncclient.operations import RPCError
from lxml import etree
from conftest import connect

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
