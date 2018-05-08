#!/bin/sh
# Arrange build environment
rm -rf build
mkdir build
cp mux-2.12.0.3.unix.tar.gz inside.sh build
# Build TinyMUX
sudo docker run --rm -v ${PWD}/build:/build -w /build alpine:latest ./inside.sh
# Build Image
sudo docker build -t tinymux:2.12.0.3 .
