#!/bin/bash

RELEASE=3.5.2

##########################################
# helper functions
error() {
	printf "\033[1;31m>> $@ \033[m\n"
	exit 1
}
warning() {
	printf "\033[1;33m>> $@ \033[m\n"
}
info() {
	printf "\033[1;32m>> $@ \033[m\n"
}
verbose() {
	if [ ${BUILD_VERBOSE} -gt 0 ]; then
		printf "$@\n"
	fi
}

##########################################
# build

# handle args
BUILD_JOB_COUNT=0
ENABLE_LTO=0
for arg in "$@"; do
	case $arg in
		# NOTE: this overwrites the detected job/cpu count
		"-j"*)
			BUILD_JOB_COUNT=$(echo $arg | cut -c 3-)
			if [ -z ${BUILD_JOB_COUNT} ]; then
				BUILD_JOB_COUNT=0
			fi
			;;
		"-lto")
			ENABLE_LTO=1
			;;
		*)
			warning "unknown argument: ${arg}"
			;;
	esac
done

# download src archives (if not already done)
if [ ! -f llvm-${RELEASE}.src.tar.xz ]; then
	curl -o llvm-${RELEASE}.src.tar.xz http://llvm.org/releases/${RELEASE}/llvm-${RELEASE}.src.tar.xz
fi
if [ ! -f cfe-${RELEASE}.src.tar.xz ]; then
	curl -o cfe-${RELEASE}.src.tar.xz http://llvm.org/releases/${RELEASE}/cfe-${RELEASE}.src.tar.xz
fi
if [ ! -f libcxx-${RELEASE}.src.tar.xz ]; then
	curl -o libcxx-${RELEASE}.src.tar.xz http://llvm.org/releases/${RELEASE}/libcxx-${RELEASE}.src.tar.xz
fi

# always clone anew
rm -Rf SPIR-Tools 2>/dev/null
git clone git://github.com/KhronosGroup/SPIR-Tools.git

rm -Rf applecl-encoder 2>/dev/null
git clone git://github.com/a2flo/applecl-encoder

# clean up prior source and build folders
rm -Rf llvm 2>/dev/null
rm -Rf libcxx 2>/dev/null
rm -Rf build 2>/dev/null

# untar and move
tar xfv llvm-${RELEASE}.src.tar.xz
tar xfv cfe-${RELEASE}.src.tar.xz
tar xfv libcxx-${RELEASE}.src.tar.xz

mv llvm-${RELEASE}.src llvm
mv cfe-${RELEASE}.src llvm/tools/clang
mv libcxx-${RELEASE}.src libcxx

# cp in spir-encoder, spir-verifier and applecl-encoder
SPIR_ENC_PATH=SPIR-Tools/spir-encoder/llvm_3.5_spir_encoder
mkdir llvm/tools/spir-encoder
cp ${SPIR_ENC_PATH}/driver/*.cpp llvm/tools/spir-encoder/
cp ${SPIR_ENC_PATH}/encoder/*.cpp llvm/tools/spir-encoder/
cp ${SPIR_ENC_PATH}/encoder/*.h llvm/tools/spir-encoder/
cp ${SPIR_ENC_PATH}/README llvm/tools/spir-encoder/
cp llvm/lib/Bitcode/Writer/ValueEnumerator.h llvm/tools/spir-encoder/
printf "[component_0]\ntype = Tool\nname = spir-encoder\nparent = Tools\nrequired_libraries = IRReader\n" > llvm/tools/spir-encoder/LLVMBuild.txt
printf "LEVEL := ../..\nTOOLNAME := spir-encoder\nLINK_COMPONENTS := irreader bitreader bitwriter\nTOOL_NO_EXPORTS := 1\ninclude \$(LEVEL)/Makefile.common\n" > llvm/tools/spir-encoder/Makefile

SPIR_VRFY_PATH=SPIR-Tools/spir-tools/spir_verifier
mkdir llvm/tools/spir-verifier
cp ${SPIR_VRFY_PATH}/driver/*.cpp llvm/tools/spir-verifier/
cp ${SPIR_VRFY_PATH}/validation/*.cpp llvm/tools/spir-verifier/
cp ${SPIR_VRFY_PATH}/validation/*.h llvm/tools/spir-verifier/
printf "[component_0]\ntype = Tool\nname = spir-verifier\nparent = Tools\nrequired_libraries = IRReader\n" > llvm/tools/spir-verifier/LLVMBuild.txt
printf "LEVEL := ../..\nTOOLNAME := spir-verifier\nLINK_COMPONENTS := irreader bitreader bitwriter\nTOOL_NO_EXPORTS := 1\ninclude \$(LEVEL)/Makefile.common\n" > llvm/tools/spir-verifier/Makefile

cp -R applecl-encoder llvm/tools/

# patch makefile, spir-encoder and spir-verifier
cd llvm
patch -p1 < ../llvm_tools_spir_applecl_encoder.patch
cd ..

# patch
cd llvm
patch -p1 < ../350_clang_llvm.patch
# NOTE: this originally came from:
# http://llvm.org/viewvc/llvm-project/llvm/trunk/include/llvm/ADT/IntrusiveRefCntPtr.h?r1=212382&r2=218295&view=patch
patch -p2 < ../gcc_5_1.patch
cd ../libcxx
patch -p1 < ../350_libcxx.patch
cd ..

# build
mkdir build
cd build

BUILD_PLATFORM=$(uname | tr [:upper:] [:lower:])
BUILD_OS="unknown"
BUILD_CPU_COUNT=1
case ${BUILD_PLATFORM} in
	"darwin")
		if expr `uname -p` : "arm.*" >/dev/null; then
			BUILD_OS="ios"
		else
			BUILD_OS="osx"
		fi
		BUILD_CPU_COUNT=$(sysctl -n hw.ncpu)
		export CXX=clang++
		export CC=clang
		;;
	"linux")
		BUILD_OS="linux"
		# note that this includes hyper-threading and multi-socket systems
		BUILD_CPU_COUNT=$(cat /proc/cpuinfo | grep "processor" | wc -l)
		export CXX=g++
		export CC=gcc
		;;
	"freebsd")
		BUILD_OS="freebsd"
		BUILD_CPU_COUNT=$(sysctl -n hw.ncpu)
		export CXX=clang++
		export CC=clang
		;;
	"openbsd")
		BUILD_OS="openbsd"
		BUILD_CPU_COUNT=$(sysctl -n hw.ncpu)
		export CXX=eg++
		export CC=egcc
		;;
	"mingw"*)
		BUILD_OS="mingw"
		BUILD_CPU_COUNT=$(env | grep 'NUMBER_OF_PROCESSORS' | sed -E 's/.*=([:digit:]*)/\1/g')
		export CXX=g++
		export CC=gcc
		;;
	*)
		warning "unknown build platform - trying to continue! ${BUILD_PLATFORM}"
		;;
esac

# if job count is unspecified (0), set it to #cpus
if [ ${BUILD_JOB_COUNT} -eq 0 ]; then
	BUILD_JOB_COUNT=${BUILD_CPU_COUNT}
fi
info "using ${BUILD_JOB_COUNT} build jobs"

EXTRA_OPTIONS=""
if [ $BUILD_OS == "osx" ]; then
	EXTRA_OPTIONS="-mmacosx-version-min=10.9"
elif [ $BUILD_OS == "ios" ]; then
	EXTRA_OPTIONS="-miphoneos-version-min=9.0"
fi

CONFIG_OPTIONS=
CLANG_OPTIONS=
if [ $CXX == "clang++" ]; then
	CONFIG_OPTIONS="--enable-libcpp"
	CLANG_OPTIONS="-fvectorize"
	if [ $ENABLE_LTO -gt 0 ]; then
		CLANG_OPTIONS="${CLANG_OPTIONS} -flto"
	fi
fi
if [ $BUILD_OS == "linux" ]; then
	CONFIG_OPTIONS="${CONFIG_OPTIONS} --prefix=/usr --sysconfdir=/etc --with-binutils-include=/usr/include --with-python=/usr/bin/python2"
fi

CC=${CC} CXX=${CXX} ../llvm/configure ${CONFIG_OPTIONS} --enable-optimized --disable-assertions --enable-cxx11 --enable-targets="host,nvptx" --disable-docs --disable-jit --disable-bindings --with-optimize-option="-Ofast -msse4.1 ${CLANG_OPTIONS} -funroll-loops -mtune=native" --with-extra-options="${EXTRA_OPTIONS}"
make -j ${BUILD_JOB_COUNT}
make_ret_code=$?

# get out of the "build" folder
cd ..
if [ ${make_ret_code} -ne 0 ]; then
	error "toolchain compilation failed!"
else
	info ""
	info "#########################"
	info "# clang/llvm build done #"
	info "#########################"
	info ""
fi
