#!/usr/bin/env python3
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
    response = m.copy_config(source='running', target='startup').xml
except RPCError as e:
    response = e.message
finally:
    print(response)
    m.close_session()
