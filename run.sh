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

# Check libssh
if [ ! -d libssh ]; then
        echo "Building libssh from source."
        git clone --depth 1 https://git.libssh.org/projects/libssh.git
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
fi
if [ ! -f libssh/build/lib/libssh.so ]; then
        cd libssh; mkdir build; cd build
        cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DGLOBAL_BIND_CONFIG=./libssh_server_config ..
        make install DESTDIR=$BUILD
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
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
        cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INCLUDE_PATH=$BUILD/usr/include -DCMAKE_LIBRARY_PATH=$BUILD/usr/lib/ ..
        make install DESTDIR=$BUILD
        rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
        cd $BUILD
fi

# Build
# export PKG_CONFIG_SYSROOT_DIR=$BUILD
export CFLAGS=-I$BUILD/usr/include
export LDFLAGS="-L$BUILD/usr/lib -lssh"
export PKG_CONFIG_PATH=$BUILD/usr/lib/pkgconfig
# export PKG_CONFIG_ALLOW_SYSTEM_LIBS=1
# export PKG_CONFIG_LIBDIR=$BUILD/usr/lib/pkgconfig
if [ ! -f $BUILD/../Makefile ]; then
    cd $BUILD/../
    ./autogen.sh
    ./configure
    rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi
    cd $BUILD
fi
make -C $BUILD/../
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# Generate host key id needed
if [ ! -f $BUILD/ssh_host_rsa_key ]; then
    ssh-keygen -b 2048 -t rsa -f $BUILD/ssh_host_rsa_key -q -N ""
fi
echo "hostkeyalgorithms ssh-rsa" > $BUILD/libssh_server_config

# Start Apteryx and populate the database
export LD_LIBRARY_PATH=$BUILD/usr/lib
$BUILD/usr/bin/apteryxd -b
$BUILD/usr/bin/apteryx -s /test/debug enable
$BUILD/usr/bin/apteryx -s /test/counter 42
$BUILD/usr/bin/apteryx -s /test/enable true

# Start restconf
sudo LD_LIBRARY_PATH=$BUILD/usr/lib \
LIBYANG_EXTENSIONS_PLUGINS_DIR=$BUILD/usr/lib/libyang1/extensions \
LIBYANG_USER_TYPES_PLUGINS_DIR=$BUILD/src/user_types \
../src/apteryx-netconf -v --key $BUILD/ssh_host_rsa_key --models $BUILD/../models/

# Stop restconf
sudo killall apteryx-netconf
# Stop Apteryx
$BUILD/usr/bin/apteryx -t
killall apteryxd
