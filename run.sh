#!/bin/bash
ROOT=`pwd`

# Check required libraries and tools
if ! pkg-config --exists glib-2.0 libxml-2.0 cunit jansson; then
        echo "Please install glib-2.0, libxml-2.0 and cunit"
        echo "(sudo apt-get install libglib2.0-dev libxml2-dev libcunit1-dev libjansson-dev)"
        exit 1
fi

# Build needed packages
BUILD=$ROOT/.build
mkdir -p $BUILD
cd $BUILD

# Check Apteryx install
if [ ! -d apteryx ]; then
        echo "Downloading Apteryx"
        git clone --depth 1 https://github.com/alliedtelesis/apteryx.git
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
fi
if [ ! -f apteryx/libapteryx.so ]; then
        echo "Building Apteryx"
        cd apteryx
        make install DESTDIR=$BUILD
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
        cd $BUILD
fi

# Check Apteryx XML Schema library
if [ ! -d apteryx-xml ]; then
        echo "Downloading apteryx-xml"
        git clone --depth 1 https://github.com/alliedtelesis/apteryx-xml.git
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
fi
if [ ! -f apteryx-xml/libapteryx-schema.so ]; then
        echo "Building apteryx-xml"
        cd apteryx-xml
        rm -f $BUILD/usr/lib/libapteryx-xml.so
        rm -f $BUILD/usr/lib/libapteryx-schema.so
        make install DESTDIR=$BUILD APTERYX_PATH=$BUILD/apteryx
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
        cd $BUILD
fi

# Check openssh
if [ ! -d openssh ]; then
        echo "Downloading openssh"
        git clone --depth 1 --branch V_8_8_P1 git://anongit.mindrot.org/openssh.git
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
fi
if [ ! -f openssh/sshd ]; then
        echo "Building openssh"
        cd openssh
        autoreconf -fvi
        ./configure --prefix=/usr --with-privsep-path=$BUILD/empty --with-privsep-user=manager
        make install-nokeys DESTDIR=$BUILD
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
        cd $BUILD
        mkdir -p $BUILD/empty
        chmod 755 $BUILD/empty
        sudo chown root:sys $BUILD/empty
fi
if [ ! -f $BUILD/ssh_host_rsa_key ]; then
    $BUILD/usr/bin/ssh-keygen -b 2048 -t rsa -f $BUILD/ssh_host_rsa_key -q -N ""
fi
echo -e "
HostKey $BUILD/ssh_host_rsa_key
HostKeyAlgorithms ssh-rsa,ssh-dss
Port 830
Subsystem netconf /usr/bin/socat STDIO UNIX:$BUILD/apteryx-netconf.sock
" > $BUILD/sshd_config

# Build
echo "Building apteryx-netconf"
export CFLAGS="-g -Wall -Werror -I$BUILD/usr/include"
export LDFLAGS=-L$BUILD/usr/lib
if [ ! -f $BUILD/../Makefile ]; then
    cd $BUILD/../
    ./autogen.sh
    ./configure \
        APTERYX_CFLAGS=-I$BUILD/usr/include APTERYX_LIBS=-lapteryx \
        APTERYX_XML_CFLAGS=-I$BUILD/usr/include APTERYX_XML_LIBS=-lapteryx-schema
    rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
    cd $BUILD
fi
make -C $BUILD/../
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# Start Apteryx and populate the database
export LD_LIBRARY_PATH=$BUILD/usr/lib
rm -f /tmp/apteryx
$BUILD/usr/bin/apteryxd -b

# Start sshd
sudo useradd -M -p $(perl -e 'print crypt($ARGV[0], "password")' 'friend') manager
sudo $BUILD/usr/sbin/sshd -f $BUILD/sshd_config

# Start restconf
rm -f $BUILD/apteryx-netconf.sock
# TEST_WRAPPER="gdb -ex run --args"
# TEST_WRAPPER="valgrind --leak-check=full"
# TEST_WRAPPER="valgrind --tool=cachegrind"
sudo LD_LIBRARY_PATH=$BUILD/usr/lib \
    LIBYANG_USER_TYPES_PLUGINS_DIR=$BUILD/src/user_types \
    $TEST_WRAPPER ../apteryx-netconf -v --models $BUILD/../models/ --unix $BUILD/apteryx-netconf.sock
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# Stop restconf
sudo killall apteryx-netconf
sudo rm /tmp/apteryx-netconf
# Stop sshd
sudo kill `pidof $BUILD/usr/sbin/sshd`
sudo userdel manager
# Stop Apteryx
$BUILD/usr/bin/apteryx -t
killall apteryxd
