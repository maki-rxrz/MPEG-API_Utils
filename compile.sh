#!/bin/bash

if [ -d ".git" ] ; then
    REV=`git rev-list HEAD | wc -l | awk '{print $1}'`
else
    REV="0"
fi

test -d bin || mkdir bin

cd src

if [ ! -f "config.h" ] ; then
cat > config.h << EOF
#define REVISION_NUMBER     "$REV"
EOF
fi

make lib
make
#make CROSS=x86_64-w64-mingw32-
#make CFLAGS="-Wall -std=gnu99 -m64" LDFLAGS="-m64"
