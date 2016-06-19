#!/usr/bin/env bash

cwd=$(cd $(dirname $0); pwd)

#-------------------------------------------------------------------------------
# example:
#-------------------------------------------------------------------------------
# $ compile.sh CROSS=x86_64-w64-mingw32-
# $ compile.sh CFLAGS="-m64" LDFLAGS="-m64"
# $ compile.sh CROSS=x86_64-w64-mingw32- CFLAGS="-mfpmath=sse -msse2"
# $ compile.sh CROSS=x86_64-w64-mingw32- BINDIR="../bin/x64" MAKEOPT="-j4"
#-------------------------------------------------------------------------------

BIN_DIR="${cwd}/bin"
COMPILERS=''

# Parse user specified options.
for opt do
    optarg="${opt#*=}"
    case "${opt}" in
        CROSS=*)
            CROSS_PREFIX="${optarg}"
        ;;
        CFLAGS=*)
            EXTRA_CFLAGS="${optarg}"
        ;;
        CPPFLAGS=*)
            EXTRA_CPPFLAGS="${optarg}"
        ;;
        LDFLAGS=*)
            EXTRA_LDFLAGS="${optarg}"
        ;;
        BINDIR=*)
            USER_BINDIR="${optarg}"
        ;;
        MAKEOPT=*)
            MAKE_OPT="${optarg}"
        ;;
        THREADS=*)
            THREAD_LIBS="${optarg}"
        ;;
        CC=*)
            CC_EXE="${optarg}"
        ;;
        LD=*)
            LD_EXE="${optarg}"
        ;;
        SHARED=*)
            ENABLE_SHARED="${optarg}"
        ;;
    esac
done

if [[ "$#" != '0' ]] ; then
    echo "[ User specified options ]"
    [ -n "${MAKE_OPT}"       ] && echo "MAKE_OPT : ${MAKE_OPT}"
    [ -n "${USER_BINDIR}"    ] && echo "BINDIR   : ${USER_BINDIR}"    && BIN_DIR="${USER_BINDIR}"
    [ -n "${CROSS_PREFIX}"   ] && echo "CROSS    : ${CROSS_PREFIX}"
    [ -n "${EXTRA_CFLAGS}"   ] && echo "CFLAGS   : ${EXTRA_CFLAGS}"
    [ -n "${EXTRA_CPPFLAGS}" ] && echo "CPPFLAGS : ${EXTRA_CPPFLAGS}"
    [ -n "${EXTRA_LDFLAGS}"  ] && echo "LDFLAGS  : ${EXTRA_LDFLAGS}"
    [ -n "${THREAD_LIBS}"    ] && echo "THREADS  : ${THREAD_LIBS}"
    [ -n "${CC_EXE}"         ] && echo "CC       : ${CC_EXE}"         && COMPILERS+=" CC=${CC_EXE}"
    [ -n "${LD_EXE}"         ] && echo "LD       : ${LD_EXE}"         && COMPILERS+=" LD=${LD_EXE}"
    [ -n "${ENABLE_SHARED}"  ] && echo "SHARED   : ${ENABLE_SHARED}"
    echo ""
fi

# Get revision number.
if [[ -d "${cwd}/.git" ]] ; then
    REV=`git rev-list --count HEAD`
else
    REV='0'
fi

# Prepare to making.
cd "${cwd}/src"

if [[ ! -f "config.h" ]] ; then
cat > config.h << EOF
#define REVISION_NUMBER     "${REV}"
EOF
fi

# Make libs and utils.
make lib \
    ${COMPILERS} \
    CROSS="${CROSS_PREFIX}" XCFLAGS="${EXTRA_CFLAGS} ${EXTRA_CPPFLAGS}" XLDFLAGS="${EXTRA_LDFLAGS}" \
    BIN_DIR="${BIN_DIR}" THREAD_LIBS="${THREAD_LIBS}" ${MAKE_OPT}
make \
    ${COMPILERS} \
    CROSS="${CROSS_PREFIX}" XCFLAGS="${EXTRA_CFLAGS} ${EXTRA_CPPFLAGS}" XLDFLAGS="${EXTRA_LDFLAGS}" \
    BIN_DIR="${BIN_DIR}" THREAD_LIBS="${THREAD_LIBS}" ENABLE_SHARED="${ENABLE_SHARED}" ${MAKE_OPT}
