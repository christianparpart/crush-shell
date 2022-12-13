#! /bin/sh

set -e

# Special environment variable to be used when only fetching and extracting
# embedded dependencies should be done, i.e. no system package manager is
# being invoked.
#
# set this as environment variable to ON to activate this mode.
if [ x$PREPARE_ONLY_EMBEDS = x ]
then
    PREPARE_ONLY_EMBEDS=OFF
fi

# if SYSDEP_ASSUME_YES=ON is set, then system package managers are attempted
# to install packages automatically, i.e. without confirmation.
if [ x$SYSDEP_ASSUME_YES = xON ]
then
    SYSDEP_ASSUME_YES='-y'
else
    unset SYSDEP_ASSUME_YES
fi

# {{{ sysdeps fetcher and unpacker for deps that aren't available via sys pkg mgnr
SYSDEPS_BASE_DIR="$(dirname $0)/../_deps"

SYSDEPS_DIST_DIR="$SYSDEPS_BASE_DIR/distfiles"
SYSDEPS_SRC_DIR="$SYSDEPS_BASE_DIR/sources"
SYSDEPS_CMAKE_FILE="$SYSDEPS_SRC_DIR/CMakeLists.txt"

fetch_and_unpack()
{
    NAME=$1
    DISTFILE=$2
    URL=$3
    MACRO=$4

    FULL_DISTFILE="$SYSDEPS_DIST_DIR/$DISTFILE"

    if ! test -f "$FULL_DISTFILE"; then
        if which curl &>/dev/null; then
            curl -L -o "$FULL_DISTFILE" "$URL"
        elif which wget &>/dev/null; then
            wget -O "$FULL_DISTFILE" "$URL"
        elif which fetch &>/dev/null; then
            # FreeBSD
            fetch -o "$FULL_DISTFILE" "$URL"
        else
            echo "Don't know how to fetch from the internet." 1>&2
            exit 1
        fi
    else
        echo "Already fetched $DISTFILE. Skipping."
    fi

    if ! test -d "$SYSDEPS_SRC_DIR/$NAME"; then
        echo "Extracting $DISTFILE"
        tar xzpf $FULL_DISTFILE -C $SYSDEPS_SRC_DIR
    else
        echo "Already extracted $DISTFILE. Skipping."
    fi

    if test x$MACRO = x; then
        echo "add_subdirectory($NAME EXCLUDE_FROM_ALL)" >> $SYSDEPS_CMAKE_FILE
    else
        echo "macro(CrushThirdParties_Embed_$MACRO)" >> $SYSDEPS_CMAKE_FILE
        echo "    add_subdirectory(\${CrushThirdParties_SRCDIR}/$NAME EXCLUDE_FROM_ALL)" >> $SYSDEPS_CMAKE_FILE
        echo "endmacro()" >> $SYSDEPS_CMAKE_FILE
    fi
}

fetch_and_unpack_Catch2()
{
    fetch_and_unpack \
        Catch2-2.13.10 \
        Catch2-2.13.10.tar.gz \
        https://github.com/catchorg/Catch2/archive/refs/tags/v2.13.10.tar.gz
}

fetch_and_unpack_fmtlib()
{
    fetch_and_unpack \
        fmt-9.1.0 \
        fmtlib-9.1.0.tar.gz \
        https://github.com/fmtlib/fmt/archive/refs/tags/9.1.0.tar.gz
}

fetch_and_unpack_gsl()
{
    fetch_and_unpack \
        GSL-3.1.0 \
        gsl-3.1.0.tar.gz \
        https://github.com/microsoft/GSL/archive/refs/tags/v3.1.0.tar.gz
}

fetch_and_unpack_embeds()
{
    if test x$LIBUNICODE_SRC_DIR = x; then
        local libunicode_git_sha="b1b017c466038655872e1968acfc6a9880cf5d9f"
        fetch_and_unpack \
            libunicode-$libunicode_git_sha \
            libunicode-$libunicode_git_sha.tar.gz \
            https://github.com/contour-terminal/libunicode/archive/$libunicode_git_sha.tar.gz \
            libunicode
    else
        echo "Hard linking external libunicode source directory to: $LIBUNICODE_SRC_DIR"
        MACRO="libunicode"
        echo "macro(CrushThirdParties_Embed_$MACRO)" >> $SYSDEPS_CMAKE_FILE
        echo "    add_subdirectory($LIBUNICODE_SRC_DIR libunicode EXCLUDE_FROM_ALL)" >> $SYSDEPS_CMAKE_FILE
        echo "endmacro()" >> $SYSDEPS_CMAKE_FILE
    fi
}
fetch_and_unpack_yaml_cpp()
{
    fetch_and_unpack \
        yaml-cpp-yaml-cpp-0.7.0 \
        yaml-cpp-0.7.0.tar.gz \
        https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.7.0.tar.gz
}

fetch_and_unpack_boxed()
{
    local boxed_cpp_git_sha="daa702e22e71f3da3eef838e4946b6c3df1f16b1"
    fetch_and_unpack \
        boxed-cpp-$boxed_cpp_git_sha \
        boxed-cpp-$boxed_cpp_git_sha.tar.gz \
        https://github.com/contour-terminal/boxed-cpp/archive/$boxed_cpp_git_sha.tar.gz \
        boxed_cpp
}

prepare_fetch_and_unpack()
{
    mkdir -p "${SYSDEPS_BASE_DIR}"
    mkdir -p "${SYSDEPS_DIST_DIR}"
    mkdir -p "${SYSDEPS_SRC_DIR}"

    # empty out sysdeps CMakeLists.txt
    rm -f $SYSDEPS_CMAKE_FILE
}
# }}}

install_deps_popos()
{
    local packages="
        build-essential
        cmake
        debhelper
        dpkg-dev
        extra-cmake-modules
        g++
        libc6-dev
        libyaml-cpp-dev
        make
        ncurses-bin
        pkg-config
    "

    RELEASE=`grep VERSION_ID /etc/os-release | cut -d= -f2 | tr -d '"'`

    local NAME=`grep ^NAME /etc/os-release | cut -d= -f2 | cut -f1 | tr -d '"'`

    fetch_and_unpack_gsl
    fetch_and_unpack_fmtlib
    fetch_and_unpack_Catch2
    fetch_and_unpack_boxed

    [ x$PREPARE_ONLY_EMBEDS = xON ] && return

    sudo apt install $SYSDEP_ASSUME_YES $packages
    # sudo snap install --classic powershell
}

install_deps_ubuntu()
{
    local packages="
        build-essential
        cmake
        debhelper
        dpkg-dev
        extra-cmake-modules
        g++
        libc6-dev
        libyaml-cpp-dev
        make
        ncurses-bin
        pkg-config
    "

    RELEASE=`grep VERSION_ID /etc/os-release | cut -d= -f2 | tr -d '"'`

    local NAME=`grep ^NAME /etc/os-release | cut -d= -f2 | cut -f1 | tr -d '"'`

    if [ ! "${NAME}" = "Debian GNU/Linux" ]; then
        # We cannot use [[ nor < here because that's not in /bin/sh.
        if [ "$RELEASE" = "18.04" ]; then
            # Old Ubuntu's (especially 18.04 LTS) doesn't have a proper std::filesystem implementation.
            packages="$packages g++-8"
        fi
    fi

    fetch_and_unpack_boxed
    fetch_and_unpack_gsl
    case $RELEASE in
        "18.04" | "19.04" | "20.04" | "21.04" | "21.10" | "22.04")
            # Older Ubuntu's don't have a recent enough fmt, so supply it.
            fetch_and_unpack_fmtlib
            fetch_and_unpack_Catch2
            ;;
        *)
            packages="$packages libfmt-dev catch2"
            ;;
    esac

    [ x$PREPARE_ONLY_EMBEDS = xON ] && return

    sudo apt install $SYSDEP_ASSUME_YES $packages
    # sudo snap install --classic powershell
}

install_deps_FreeBSD()
{
    fetch_and_unpack_fmtlib
    fetch_and_unpack_boxed

    [ x$PREPARE_ONLY_EMBEDS = xON ] && return

    # NB: libfmt is available in pkg, but it's not version >= 9.0.0 (as of 2022-09-03).
    su root -c "pkg install $SYSDEP_ASSUME_YES \
        catch \
        cmake \
        microsoft-gsl \
        ncurses \
        ninja \
        pkgconf \
        yaml-cpp
    "
}

install_deps_arch()
{
    fetch_and_unpack_fmtlib
    fetch_and_unpack_boxed
    [ x$PREPARE_ONLY_EMBEDS = xON ] && return

    sudo pacman -S -y \
        catch2 \
        cmake \
        extra-cmake-modules \
        git \
        microsoft-gsl \
        ninja \
        pkg-config \
        yaml-cpp
}

install_deps_suse()
{
    fetch_and_unpack_gsl
    fetch_and_unpack_fmtlib
    fetch_and_unpack_boxed

    echo "SuSE: PREPARE_ONLY_EMBEDS=$PREPARE_ONLY_EMBEDS"
    [ x$PREPARE_ONLY_EMBEDS = xON ] && return

    local packages="
        Catch2-devel
        cmake
        extra-cmake-modules
        gcc-c++
        ncurses-devel
        ninja
        pkgconf
        yaml-cpp-devel
    "
    # Sadly, gsl-devel system package is too old to be used.
    sudo zypper install $SYSDEP_ASSUME_YES $packages
}

install_deps_fedora()
{
    fetch_and_unpack_gsl
    fetch_and_unpack_fmtlib
    fetch_and_unpack_Catch2
    fetch_and_unpack_boxed
    [ x$PREPARE_ONLY_EMBEDS = xON ] && return

    local packages="
        cmake
        extra-cmake-modules
        gcc-c++
        ninja-build
        pkgconf
        yaml-cpp-devel
    "

    # Sadly, gsl-devel system package is too old to be used.
    sudo dnf install $SYSDEP_ASSUME_YES $packages
}


install_deps_darwin()
{
    # NB: catch2 is available on brew but version 3, and we are still using version 2
    # due to all the other supported platforms.
    fetch_and_unpack_Catch2
    fetch_and_unpack_boxed

    [ x$PREPARE_ONLY_EMBEDS = xON ] && return

    # NB: Also available in brew: mimalloc
    brew install $SYSDEP_ASSUME_YES \
        cpp-gsl \
        fmt \
        pkg-config \
        yaml-cpp
}

main()
{
    if test x$OS_OVERRIDE != x; then
        # In CI, we need to be able to fetch embedd-setups for different OSes.
        ID=$OS_OVERRIDE
    elif test -f /etc/os-release; then
        ID=`grep ^ID= /etc/os-release | cut -d= -f2`
    else
        ID=`uname -s`
    fi

    # Strip double-quotes, as used by opensuse for interesting reason.
    ID=`echo $ID | tr -d '"'`

    prepare_fetch_and_unpack

    case "$ID" in
        arch|manjaro)
            install_deps_arch
            ;;
        opensuse*)
            install_deps_suse
            ;;
        fedora)
            install_deps_fedora
            ;;
        pop)
            install_deps_popos
            ;;
        ubuntu|neon|debian)
            install_deps_ubuntu
            ;;
        Darwin)
            install_deps_darwin
            ;;
        freebsd)
            install_deps_FreeBSD
            ;;
        *)
            fetch_and_unpack_Catch2
            fetch_and_unpack_fmtlib
            fetch_and_unpack_gsl
            fetch_and_unpack_yaml_cpp
            fetch_and_unpack_boxed
            echo "OS $ID not supported."
            echo "Please install the remaining dependencies manually."
            ;;
    esac

    fetch_and_unpack_embeds
}

main $*
