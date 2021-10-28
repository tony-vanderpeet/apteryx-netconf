#!/usr/bin/env python3
import sys
from ncclient import manager
from ncclient.operations import RPCError

m = manager.connect(host='localhost',
                    port=830,
                    username='manager',
                    password='friend',
                    hostkey_verify=False,
                    allow_agent=False,
                    look_for_keys=False)
try:
    xpath = sys.argv[1] if len(sys.argv) > 1 else '/test/counter'
    response = m.get_config(source='running', filter=('xpath', xpath)).data_xml
except RPCError as e:
    response = e.message
finally:
    print(response)
    m.close_session()