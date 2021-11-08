#!/bin/bash
ROOT=`pwd`

# Check required libraries and tools
if ! pkg-config --exists glib-2.0 gio-2.0 openssl; then
        echo "Please install glib-2.0 (sudo apt-get install libglib2.0-dev openssl)"
        exit 1
fi

# Build needed packages
BUILD=$ROOT/.build
mkdir -p $BUILD
cd $BUILD

# Check Apteryx install
if [ ! -d apteryx ]; then
        echo "Building Apteryx from source."
        git clone --depth 1 https://github.com/alliedtelesis/apteryx.git
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
fi
if [ ! -f apteryx/libapteryx.so ]; then
        cd apteryx
        make install DESTDIR=$BUILD
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
        cd $BUILD
fi

# Check libyang
if [ ! -d libyang ]; then
        echo "Building libyang from source."
        git clone --depth 1 --branch v1.0.240 https://github.com/CESNET/libyang.git
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
fi
if [ ! -f libyang/build/libyang.so ]; then
        cd libyang; mkdir build; cd build
        cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib ..
        make install DESTDIR=$BUILD
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
        ln -s src libyang
        cd $BUILD
fi

# Check libnetconf2
if [ ! -d libnetconf2 ]; then
        echo "Building libnetconf2 from source."
        git clone --depth 1 --branch v1.1.46 https://github.com/CESNET/libnetconf2.git
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
fi
if [ ! -f libnetconf2/build/libnetconf2.so ]; then
        cd libnetconf2; mkdir -p build; cd build
        cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INCLUDE_PATH=$BUILD/usr/include \
            -DCMAKE_LIBRARY_PATH=$BUILD/usr/lib/ -DENABLE_TLS=OFF -DENABLE_SSH=OFF -DENABLE_DNSSEC=OFF ..
        make install DESTDIR=$BUILD
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
        cd $BUILD
fi

# Check openssh
if [ ! -d openssh ]; then
        echo "Building openssh from source."
        git clone --depth 1 --branch V_8_8_P1 git://anongit.mindrot.org/openssh.git
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
fi
if [ ! -f openssh/sshd ]; then
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
Subsystem netconf socat STDIO UNIX:$BUILD/apteryx-netconf.sock
" > $BUILD/sshd_config

# Build
export CFLAGS="-g -Wall -Werror -I$BUILD/usr/include"
export LDFLAGS=-L$BUILD/usr/lib
export PKG_CONFIG_PATH=$BUILD/usr/lib/pkgconfig
if [ ! -f $BUILD/../Makefile ]; then
    cd $BUILD/../
    ./autogen.sh
    ./configure
    rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
    cd $BUILD
fi
make -C $BUILD/../
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# Start Apteryx and populate the database
export LD_LIBRARY_PATH=$BUILD/usr/lib
$BUILD/usr/bin/apteryxd -b

# Start sshd
sudo useradd -M -p $(perl -e 'print crypt($ARGV[0], "password")' 'friend') manager
sudo $BUILD/usr/sbin/sshd -f $BUILD/sshd_config

# Start restconf
# TEST_WRAPPER="gdb --args"
# TEST_WRAPPER="valgrind --leak-check=full"
# TEST_WRAPPER="valgrind --tool=cachegrind"
sudo LD_LIBRARY_PATH=$BUILD/usr/lib \
    LIBYANG_EXTENSIONS_PLUGINS_DIR=$BUILD/usr/lib/libyang1/extensions \
    LIBYANG_USER_TYPES_PLUGINS_DIR=$BUILD/src/user_types \
    $TEST_WRAPPER ../src/apteryx-netconf -v --models $BUILD/../models/ --unix $BUILD/apteryx-netconf.sock
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
