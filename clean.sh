#!/bin/sh

cwd=$(cd $(dirname $0); pwd)

cd $cwd/src
make clean
