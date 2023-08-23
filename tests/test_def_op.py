import pytest
from ncclient.operations import RPCError
from lxml import etree
from conftest import connect

# EDIT-CONFIG with default-operation


def _error_check(err, expect_err):
    assert err is not None
    assert err.tag == expect_err["tag"]
    assert err.type == expect_err["type"]
    assert err.severity == "error"
    if "message" in expect_err:
        assert err.message == expect_err["message"]


def _def_op_test(payload, expect_err=None, post_xpath=None, inc_str=[], exc_str=[], def_op="merge"):
    """
    Run an edit-config with the given payload, and default-operation parameter,
    optionally checking for error, and strings that should be included or excluded in the response,
    returning the response from a get carried out with the optional given xpath.
    """
    m = connect()
    xml = None
    try:
        response = m.edit_config(target='running', config=payload, default_operation=def_op)
        print(response)
    except RPCError as err:
        print(err)
        assert expect_err is not None
        _error_check(err, expect_err)
    else:
        assert expect_err is None
        if post_xpath is not None:
            xml = m.get(filter=('xpath', post_xpath)).data
            print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
            if len(inc_str) + len(exc_str) != 0:
                for s in inc_str:
                    assert s in etree.XPath("//text()")(xml)
                for s in exc_str:
                    assert s not in etree.XPath("//text()")(xml)
    finally:
        m.close_session()
    return xml


def test_def_op_none():
    """
    Valid edit, but default operation is none.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority>5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="none", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority').text != '5'


def test_def_op_none_merge():
    """
    Default operation is none, variable is a merge.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="merge">5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="none", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '5'


def test_def_op_none_replace():
    """
    Default operation is none, variable is a replace.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="replace">5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="none", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '5'


def test_def_op_none_remove():
    """
    Default operation is none, variable is a remove.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="remove">5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="none", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority') is None


def test_def_op_none_delete():
    """
    Default operation is none, variable is a delete.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="delete">5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="none", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority') is None


def test_def_op_none_create():
    """
    Default operation is none, variable is a create.
    """
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
    _def_op_test(payload, def_op="none", post_xpath='/test/animals', inc_str=["penguin"])


def test_def_op_merge():
    """
    Valid edit, but default operation is merge.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority>5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="merge", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '5'


def test_def_op_merge_merge():
    """
    Default operation is merge, variable is merge.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="merge">5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="merge", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '5'


def test_def_op_merge_replace():
    """
    Default operation is merge, variable is replace.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="replace">5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="merge", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority').text == '5'


def test_def_op_merge_remove():
    """
    Default operation is merge, variable is remove.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="remove">5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="merge", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority') is None


def test_def_op_merge_delete():
    """
    Default operation is merge, variable is delete.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="delete">5</priority>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, def_op="merge", post_xpath='/test/settings/priority')
    assert xml.find('./{*}test/{*}settings/{*}priority') is None


def test_def_op_merge_create():
    """
    Default operation is merge, variable is create.
    """
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
    _def_op_test(payload, def_op="merge", post_xpath='/test/animals', inc_str=["penguin"])


def test_def_op_replace():
    """
    Valid edit, but default operation is replace. This should replace everything from test down.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority>5</priority>
    </settings>
  </test>
</config>
"""
    _def_op_test(payload, def_op="replace", post_xpath='/test', exc_str=["true", "50", "cat", "dog", "mouse"], inc_str=["5"])


def test_def_op_replace_merge():
    """
    Default operation is replace, with specific variable a merge. This should fail, as it doesn't make sense.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="merge">5</priority>
    </settings>
  </test>
</config>
"""
    _def_op_test(payload, def_op="replace",
                 expect_err={"tag": "operation-not-supported", "type": "protocol"})


def test_def_op_replace_replace():
    """
    Default operation is replace. Variable is replace. This should replace everything from test down.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
        <priority xc:operation="replace">5</priority>
    </settings>
  </test>
</config>
"""
    _def_op_test(payload, def_op="replace", post_xpath='/test', exc_str=["true", "50", "cat", "dog", "mouse"], inc_str=["5"])

# EDIT-CONFIG (operation="delete")


def test_def_op_none_merge_delete():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings xc:operation="merge">
      <priority>5</priority>
    </settings>
    <animals xc:operation="merge">
        <animal xc:operation="delete">
            <name>cat</name>
        </animal>
        <animal>
          <name>hippogriff</name>
          <colour>purple</colour>
        </animal>
    </animals>
  </test>
</config>
"""
    _def_op_test(payload, post_xpath="/test", inc_str=["5", "purple"], exc_str=["cat"])


def xxxtest_edit_config_merge_delete():
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
    xml = _def_op_test(payload, post_xpath='/test/settings')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert etree.XPath("//text()")(xml) == ['enable', 'false', '1']


# EDIT-CONFIG (operation=replace)
#  replace:  The configuration data identified by the element
#     containing this attribute replaces any related configuration
#     in the configuration datastore identified by the <target>
#     parameter.  If no such configuration data exists in the
#     configuration datastore, it is created.  Unlike a
#     <copy-config> operation, which replaces the entire target
#     configuration, only the configuration actually present in
#     the <config> parameter is affected.

def xxxtest_edit_config_replace_list_item():
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
    _def_op_test(payload, post_xpath="/test/animals/animal[name='cat']", inc_str=["brown"], exc_str=["big"])


def xxxtest_edit_config_replace_all():
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
    _def_op_test(payload, post_xpath='/test/animals', inc_str=["cat"], exc_str=["dog", "mouse"])


def xxxtest_edit_config_replace_one_full():
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
    xml = _def_op_test(payload, post_xpath='/test/animals', inc_str=["cat", "dog", "mouse"])
    assert xml.find('./{*}test/{*}animals/{*}animal[{*}name="cat"]/{*}type').text == 'little'
    assert xml.find('./{*}test/{*}animals/{*}animal[{*}name="cat"]/{*}colour').text == 'tawny'


@pytest.mark.skip(reason="nothing found, no defaults even")
def xxxtest_edit_config_replace_one_default():
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
    xml = _def_op_test(payload, post_xpath='/test/animals', inc_str=["cat", "dog", "mouse"])
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert xml.find('./{*}test/{*}animals/{*}animal[name="mouse"]/{*}type').text == 'big'
    assert xml.find('./{*}test/{*}animals/{*}animal[name="mouse"]/{*}colour') is None


# EDIT-CONFIG (operation=create)
#  create:  The configuration data identified by the element
#     containing this attribute is added to the configuration if
#     and only if the configuration data does not already exist in
#     the configuration datastore.  If the configuration data
#     exists, an <rpc-error> element is returned with an
#     <error-tag> value of "data-exists".

def xxxtest_edit_config_create_list_item():
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
    _def_op_test(payload, post_xpath="/test/animals", inc_str=["penguin"])


def xxxtest_edit_config_create_list_item_exists():
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
    _def_op_test(payload, expect_err="data-exists")


def xxxtest_edit_config_create_list_item_field():
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
    _def_op_test(payload, post_xpath="/test/animals", inc_str=["white"])


def xxxtest_edit_config_create_list_item_field_exists():
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
    _def_op_test(payload, expect_err="data-exists")


# EDIT-CONFIG (operation=remove)
#  remove:  The configuration data identified by the element
#     containing this attribute is deleted from the configuration
#     if the configuration data currently exists in the
#     configuration datastore.  If the configuration data does not
#     exist, the "remove" operation is silently ignored by the
#     server.

def xxxtest_edit_config_remove_invalid_path():
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
    _def_op_test(payload, expect_err="malformed-message")


def xxxtest_edit_config_remove_missing_data():
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
    _def_op_test(payload, expect_err="malformed-message")


# Empty value for nodes that have a non-empty pattern or values

def xxxtest_edit_config_invalid_empty_merge():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
      <debug></debug>
    </settings>
  </test>
</config>
"""
    _def_op_test(payload, expect_err="bad-attribute")


def xxxtest_edit_config_invalid_empty_replace():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
      <debug xc:operation="replace"></debug>
    </settings>
  </test>
</config>
"""
    _def_op_test(payload, expect_err="bad-attribute")


def xxxtest_edit_config_empty_delete():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
      <enable xc:operation="delete"></enable>
    </settings>
  </test>
</config>
"""
    xml = _def_op_test(payload, post_xpath='/test/settings/enable')
    print(etree.tostring(xml, pretty_print=True, encoding="unicode"))
    assert etree.XPath("//text()")(xml) == []
