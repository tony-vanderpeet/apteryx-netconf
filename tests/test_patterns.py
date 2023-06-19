from ncclient.operations import RPCError
from conftest import connect

# EDIT-CONFIG with patterns
# for variables that have a pattern try a variety of correct and incorrect values to
# verify correct behaviour


def _pattern_test(payload, test_matrix):
    """
    Use payload as a format to create edit-config requests, based on the contents of
    test_matrix, which is an array of tuples containing a value and expected result.
    """
    for one_test in test_matrix:
        try:
            m = connect()
            one_payload = payload.format(pval=one_test[0])
            response = m.edit_config(target='running', config=one_payload)
            print(f"testing <{one_test[0]}>, edit result expected: {one_test[1]}")
            print(response)
        except RPCError as err:
            print(err)
            assert one_test[1] is False
        else:
            assert one_test[1] is True
        finally:
            m.close_session()


def test_pattern_debug():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
      <debug>{pval}</debug>
    </settings>
  </test>
</config>
"""
    tm = (("1", True), ("0", True), ("2", False), ("enable", True),
          ("true", False), ("Enable", False))
    _pattern_test(payload, tm)


def test_range_priority():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
      <priority>{pval}</priority>
    </settings>
  </test>
</config>
"""
    tm = (("1", True), ("-1", True), ("99", True), ("0", False), ("11", False), ("-11", False), ("18446744073709551615", False), ("-9223372036854775808", False), ("^1$", False))
    _pattern_test(payload, tm)


def test_range_volume():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <settings>
      <volume>{pval}</volume>
    </settings>
  </test>
</config>
"""
    # uint64 range="0..18446744073709551615"
    tm = (("-1", False), ("0", True), ("18446744073709551615", True), ("28446744073709551615", False))
    _pattern_test(payload, tm)


def test_range_speed():
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test xmlns="http://test.com/ns/yang/testing-2">
    <settings>
      <speed xmlns="http://test.com/ns/yang/testing2-augmented">{pval}</speed>
    </settings>
  </test>
</config>
"""
    # int64 range="-9223372036854775808..9223372036854775807"
    tm = (("-18446744073709551615", False), ("-9223372036854775808", True), ("0", True), ("9223372036854775807", True), ("18446744073709551615", False))
    _pattern_test(payload, tm)


def _patterns_variable_test(variable, test_matrix):
    """
    Generic test for testing a variable under /test/patterns.
    """
    payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"
        xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <test>
    <patterns>
      <{variable}>{pval}</{variable}>
    </patterns>
  </test>
</config>
"""
    for one_test in test_matrix:
        try:
            m = connect()
            one_payload = payload.format(variable=variable, pval=one_test[0])
            response = m.edit_config(target='running', config=one_payload)
            print(f"testing <{one_test[0]}>, edit result expected: {one_test[1]}")
            print(response)
        except RPCError as err:
            print(err)
            assert one_test[1] is False
        else:
            assert one_test[1] is True
        finally:
            m.close_session()


def test_patterns_variable_1():
    tm = (("flash:/default.cfg", True), ("usb:/a.cfg", True), ("sunny", False), ("flash:", False),
          ("flash:default", False),
          ("card:asdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfg"
           "jklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjklds"
           "jklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjklds"
           "jklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjklds"
           "jklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjkldspasdfgjklpa.cfg", False),
          ("card:asdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfg"
           "jklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjklds"
           "jklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjklds"
           "jklpasdfgjklpasdfgjklpasdfgjklpasdfgjklpasdfgjkldspasdfgjklpa.cfg", True),
          ("xyzflash:a.cfgabd", False))
    _patterns_variable_test("variable_1", tm)
