#!/bin/bash -e
#
# glc-build.sh -- glc build and install script
# Copyright (C) 2007-2008 Pyry Haulos
#

STABLE_GLC_VER=0.5.8

info () {
	echo -e "\033[32minfo\033[0m  : $1"
}

ask () {
	echo -e "        $1"
}

ask-prompt () {
	echo -ne "      \033[34m>\033[0m "
}

error () {
	echo -e "\033[31merror\033[0m : $1"
}

die () {
	error "$1"
	exit 1
}

info "Welcome to glc install script!"

BUILD64=0
[ $(uname -m) == "x86_64" ] && BUILD64=1

echo "#include <stdio.h>
	int main(int argc, char argv[]){printf(\"test\");return 0;}" | \
	gcc -x c - -o /dev/null 2> /dev/null \
	|| die "Can't compile (Ubuntu users: sudo apt-get install build-essential)"
[ -e "/usr/include/X11/X.h" -a -e "/usr/include/X11/Xlib.h" ] \
	|| die "Missing X11 headers (Ubuntu users: sudo apt-get install libx11-dev)"
[ -e "/usr/include/X11/extensions/xf86vmode.h" ] \
	|| die "Missing XF86VidMode headers (Ubuntu users: sudo apt-get install libxxf86vm-dev)"
[ -e "/usr/include/GL/gl.h" -a -e "/usr/include/GL/glx.h" ] \
	|| die "Missing OpenGL headers (Ubuntu users: sudo apt-get install libgl1-mesa-dev)"
[ -e "/usr/include/alsa/asoundlib.h" ] \
	|| die "Missing ALSA headers (Ubuntu users: sudo apt-get install libasound2-dev)"
[ -e "/usr/include/png.h" ] \
	|| die "Missing libpng headers (Ubuntu users: sudo apt-get install libpng12-dev)"
[ -x "/usr/bin/cmake" ] \
	|| die "CMake not found (Ubuntu users: sudo apt-get install cmake)"
[ -x "/usr/bin/git" ] \
	|| die "git not found (Ubuntu users: sudo apt-get install git-core)"

if [ $BUILD64 == 1 ]; then
	echo "#include <stdio.h>
		int main(int argc, char argv[]){printf(\"test\");return 0;}" | \
		gcc -m32 -x c - -o /dev/null 2> /dev/null \
		|| die "Can't compile 32-bit code (Ubuntu users: sudo apt-get install gcc-multilib)"
fi

DEFAULT_CFLAGS="-O2 -fomit-frame-pointer -mtune=generic"

ask "Enter path where glc will be installed."
ask "  (leave blank to install to root directory)"
ask-prompt
read DESTDIR
[ "${DESTDIR:${#DESTDIR}-1}" == "/" ] && DESTDIR="${DESTDIR:0:${#DESTDIR}-1}"
if [ "${DESTDIR}" != "" ]; then
	if [ -e "${DESTDIR}" ]; then
		[ -f "${DESTDIR}" ] && die "Invalid install directory"
	else
		mkdir -p "${DESTDIR}" 2> /dev/null \
			|| sudo mkdir -p "${DESTDIR}" 2> /dev/null \
			|| die "Can't create install directory"
	fi
fi

[ "${DESTDIR}" == "" ] && DESTDIR="/usr"

SUDOMAKE="sudo make"
[ -w "${DESTDIR}" ] && SUDOMAKE="make"

ask "Enter compiler optimizations."
ask "  (${DEFAULT_CFLAGS})"
ask-prompt
read CFLAGS
[ "${CFLAGS}" == "" ] && CFLAGS="${DEFAULT_CFLAGS}"

USE_GIT="n"
ask "Use latest unstable development version (y/n)"
ask-prompt
read USE_GIT

if [ -d glc ]; then
	info "Updating sources..."
	cd glc
	git pull origin
	cd ..
else
	info "Fetching sources..."
	git clone https://github.com/sfan5/glc.git glc
fi

cd glc
git submodule init
git submodule update
cd ..

if [ "${USE_GIT}" == "y" ]; then
	cd . # Do nothing
else
	cd glc
	git checkout ${STABLE_GLC_VER}
	cd ..
fi

MLIBDIR="lib"
[ $BUILD64 == 1 ] && MLIBDIR="lib64"

info "Building glc..."

[ -d glc/build ] && rm -R glc/build
mkdir glc/build
cd glc/build

cmake .. \
	-DCMAKE_INSTALL_PREFIX:PATH="${DESTDIR}" \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_C_FLAGS_RELEASE:STRING="${CFLAGS}" \
	-DMLIBDIR="${MLIBDIR}"
make

if [ $BUILD64 == 1 ]; then
	cd ..
	[ -d build32 ] && rm -R build32
	mkdir build32
	cd build32

	cmake .. \
		-DCMAKE_INSTALL_PREFIX:PATH="${DESTDIR}" \
		-DCMAKE_BUILD_TYPE:STRING="Release" \
		-DCMAKE_C_FLAGS_RELEASE:STRING="${CFLAGS} -m32" \
		-DBINARIES:BOOL=OFF \
		-DMLIBDIR="lib32"
	make
fi
cd ../..

info "Installing glc..."
cd glc/build
if [ $BUILD64 == 1 ]; then
	$SUDOMAKE install
	cd ../build32
	$SUDOMAKE install
else
	$SUDOMAKE install
fi
cd ../..

info "Done :)"

# TODO more complete escape
RDIR=`echo "${DESTDIR}" | sed 's/ /\\ /g'`

LD_LIBRARY_PATH_ADD="${RDIR}/lib"
[ $BUILD64 == 1 ] && LD_LIBRARY_PATH_ADD="${RDIR}/lib64:${RDIR}/lib32"

if [ "${DESTDIR}" != "" ]; then
	info "You may need to add following lines to your .bashrc:"
	echo "export PATH=\"\${PATH}:${RDIR}/bin\""
	echo "export LD_LIBRARY_PATH=\"\${LD_LIBRARY_PATH}:${LD_LIBRARY_PATH_ADD}\""
fi

RM="rm"
[ -w "${DESTDIR}/usr/bin/glc-play" ] || RM="sudo ${RM}"

info "If you want to remove glc, execute:"
if [ $BUILD64 == 1 ]; then
	echo "${RM} \\"
	echo "${RDIR}/lib64/libglc-core.so* \\"
	echo "${RDIR}/lib64/libglc-capture.so* \\"
	echo "${RDIR}/lib64/libglc-play.so* \\"
	echo "${RDIR}/lib64/libglc-export.so* \\"
	echo "${RDIR}/lib64/libglc-hook.so* \\"
	echo "${RDIR}/lib64/libelfhacks.so* \\"
	echo "${RDIR}/lib64/libpacketstream.so* \\"
	echo "${RDIR}/lib64/libelfhacks.so* \\"
	echo "${RDIR}/lib32/libglc-core.so* \\"
	echo "${RDIR}/lib32/libglc-capture.so* \\"
	echo "${RDIR}/lib32/libglc-play.so* \\"
	echo "${RDIR}/lib32/libglc-export.so* \\"
	echo "${RDIR}/lib32/libglc-hook.so* \\"
	echo "${RDIR}/lib32/libelfhacks.so* \\"
	echo "${RDIR}/lib32/libpacketstream.so* \\"
else
	echo "${RM} \\"
	echo "${RDIR}/lib/libglc-core.so* \\"
	echo "${RDIR}/lib/libglc-capture.so* \\"
	echo "${RDIR}/lib/libglc-play.so* \\"
	echo "${RDIR}/lib/libglc-export.so* \\"
	echo "${RDIR}/lib/libglc-hook.so* \\"
	echo "${RDIR}/lib/libelfhacks.so* \\"
	echo "${RDIR}/lib/libpacketstream.so* \\"
fi
echo "${RDIR}/include/elfhacks.h \\"
echo "${RDIR}/include/packetstream.h \\"
echo "${RDIR}/bin/glc-capture \\"
echo "${RDIR}/bin/glc-play"
