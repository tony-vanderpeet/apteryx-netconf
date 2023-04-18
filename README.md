# Apteryx-Netconf
Netconf daemon using Apteryx as its backend store.

## Requires
```
apteryx glib-2.0, libxml-2.0
```

## Building
```
./autogen.sh
./configure
make install
```

## Usage
```bash
Usage:
  apteryx-netconf [OPTION...] - Netconf access to Apteryx

Help Options:
  -h, --help        Show help options

Application Options:
  -d, --debug       Debug
  -v, --verbose     Verbose
  -m, --models      Path to yang models(defaults to "./")
  -u, --unix        Listen on unix socket (defaults to "/tmp/apteryx-netconf")
  -c, --copy        BASH command to run to copy running->startup
  -r, --remove      BASH command to run to remove startup config
```

```bash
apteryxd -b
apteryx -s /system/name alfred
apteryx -s /system/arch x86_64
apteryx-netconf -v --unix /tmp/apteryx-netconf --models models/
echo "Subsystem netconf exec socat STDIO UNIX:/tmp/apteryx-netconf" >> /etc/ssh/sshd_config
systemctl restart sshd
```

## Tests
```bash
# Build and run a local copy using openssh
./run.sh
# Run the unit tests using pytest
python3 -m pytest
python3 -m pytest -k test_server_capabilities

google-chrome .gcov/index.html
```

## Examples
```python
from ncclient import manager
m = manager.connect(host='localhost',
                    port=830,
                    username='manager',
                    password='friend',
                    hostkey_verify=False,
                    allow_agent=False,
                    look_for_keys=False)

payload = """
<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <system xmlns="http://openconfig.net/yang/system">
    <name>Alfred</name>
    <arch>x86_64</arch>
  </system>
</config>
"""
m.edit_config(target='running', config=payload).xml

print(m.get_config(source='running', filter=('xpath', '/system/name')).xml)

print(m.get(('subtree', '<system><name /></system>')).xml)

m.copy_config(source='running', target='startup')

m.delete_config(target='startup')

m.close_session()
```
