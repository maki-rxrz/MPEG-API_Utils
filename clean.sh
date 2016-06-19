#!/usr/bin/env bash

cwd=$(cd $(dirname $0); pwd)

cd ${cwd}/src
make clean
