#!/bin/sh
apk add --no-cache g++ make openssl openssl-dev
tar xzf mux-2.12.0.7.unix.tar.gz
cd mux2.12
patch -p2 < ../openssl.patch
cd src
./configure --enable-ssl
sed -e '80s/-lssl/-lssl -lcrypto/g' Makefile > Makefile.new
mv Makefile.new Makefile
make depend
make
# Startmux requires adjustment
cd ../game
sed -e '82s/(nohup /exec /g' -e '82s/ &)//' Startmux > Startmux.new
mv Startmux.new Startmux
chmod u+x Startmux
# Replace symbolic links with real files.
rm bin/*
cp ../src/netmux bin/netmux
cp ../src/libmux.so bin/libmux.so
cp ../src/slave bin/slave
