#!/bin/bash

#-------------------------------------------------------------------------------
# example:
#-------------------------------------------------------------------------------
# $ compile.sh CROSS=x86_64-w64-mingw32-
# $ compile.sh CFLAGS="-m64" LDFLAGS="-m64"
# $ compile.sh CROSS=x86_64-w64-mingw32- CFLAGS="-mfpmath=sse -msse2"
# $ compile.sh CROSS=x86_64-w64-mingw32- BINDIR="../bin/x64" MAKEOPT="-j4"
#-------------------------------------------------------------------------------

BIN_DIR="../bin"

# check options
for opt do
    optarg="${opt#*=}"
    case "$opt" in
        CROSS=*)
            CROSS_PREFIX="$optarg"
        ;;
        CFLAGS=*)
            EXTRA_CFLAGS="$optarg"
        ;;
        LDFLAGS=*)
            EXTRA_LDFLAGS="$optarg"
        ;;
        BINDIR=*)
            USER_BINDIR="$optarg"
        ;;
        MAKEOPT=*)
            MAKE_OPT="$optarg"
        ;;
        THREADS=*)
            THREAD_LIBS="$optarg"
        ;;
    esac
done

if [ "$#" != "0" ] ; then
    echo "[ User specified options ]"
    [ "$MAKE_OPT"      != "" ] && echo "MAKE_OPT : $MAKE_OPT"
    [ "$USER_BINDIR"   != "" ] && echo "BINDIR   : $USER_BINDIR"   && BIN_DIR="$USER_BINDIR"
    [ "$CROSS_PREFIX"  != "" ] && echo "CROSS    : $CROSS_PREFIX"
    [ "$EXTRA_CFLAGS"  != "" ] && echo "CFLAGS   : $EXTRA_CFLAGS"
    [ "$EXTRA_LDFLAGS" != "" ] && echo "LDFLAGS  : $EXTRA_LDFLAGS"
    [ "$THREAD_LIBS"   != "" ] && echo "THREADS  : $THREAD_LIBS"
    echo ""
fi

# check revision
if [ -d ".git" ] ; then
    REV=`git rev-list HEAD | wc -l | awk '{print $1}'`
else
    REV="0"
fi

# ready to making
cd src

if [ ! -f "config.h" ] ; then
cat > config.h << EOF
#define REVISION_NUMBER     "$REV"
EOF
fi

# make libs and utils
make lib CROSS="$CROSS_PREFIX" XCFLAGS="$EXTRA_CFLAGS" XLDFLAGS="$EXTRA_LDFLAGS" BIN_DIR="$BIN_DIR" THREAD_LIBS="$THREAD_LIBS" $MAKE_OPT
make     CROSS="$CROSS_PREFIX" XCFLAGS="$EXTRA_CFLAGS" XLDFLAGS="$EXTRA_LDFLAGS" BIN_DIR="$BIN_DIR" THREAD_LIBS="$THREAD_LIBS" $MAKE_OPT
