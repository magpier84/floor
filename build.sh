#!/bin/sh

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
# compiler setup/check

# if no CXX/CC are specified, try using clang++/clang
if [ -z "${CXX}" ]; then
	CXX=clang++
fi
if [ -z "${CC}" ]; then
	CC=clang
fi

# check if clang is the compiler, fail if not
CXX_VERSION=$(${CXX} -v 2>&1)
if expr "${CXX_VERSION}" : ".*clang" >/dev/null; then
	# also check the clang version
	if expr "${CXX_VERSION}" : "Apple.*" >/dev/null; then
		# apple xcode/llvm/clang versioning scheme -> at least 6.1.0 is required (ships with Xcode 6.3)
		if expr "$(echo ${CXX_VERSION} | head -n 1 | sed -E "s/Apple LLVM version ([0-9.]+) .*/\1/")" \< "6.1.0" >/dev/null; then
			error "at least Xcode 6.3 / clang/LLVM 6.1.0 is required to compile this project!"
		fi
	else
		# standard clang versioning scheme -> at least 3.5.0 is required
		if expr "$(echo ${CXX_VERSION} | head -n 1 | sed -E "s/.*clang version ([0-9.]+) .*/\1/")" \< "3.5.0" >/dev/null; then
			error "at least clang 3.5.0 is required to compile this project!"
		fi
	fi
else
	error "only clang is currently supported - please set CXX/CC to clang++/clang and try again!"
fi

##########################################
# arg handling
BUILD_MODE="release"
BUILD_VERBOSE=0
BUILD_JOB_COUNT=0

BUILD_CONF_OPENCL=1
BUILD_CONF_CUDA=1
BUILD_CONF_OPENAL=1
BUILD_CONF_HOST_COMPUTE=1
BUILD_CONF_METAL=1
BUILD_CONF_VULKAN=1
BUILD_CONF_NET=1
BUILD_CONF_XML=1
BUILD_CONF_EXCEPTIONS=1
BUILD_CONF_POCL=0
BUILD_CONF_LIBSTDCXX=0

BUILD_CONF_SANITIZERS=0
BUILD_CONF_ASAN=0
BUILD_CONF_MSAN=0
BUILD_CONF_TSAN=0
BUILD_CONF_UBSAN=0

BUILD_ARCH_SIZE="x64"
BUILD_ARCH=$(${CC} -dumpmachine | sed "s/-.*//")
case $BUILD_ARCH in
	"i386"|"i486"|"i586"|"i686"|"arm7"*|"armv7"*)
		BUILD_ARCH_SIZE="x32"
		;;
	"x86_64"|"amd64"|"arm64")
		BUILD_ARCH_SIZE="x64"
		;;
	*)
		warning "unknown architecture (${BUILD_ARCH}) - using ${BUILD_ARCH_SIZE}!"
		;;
esac

for arg in "$@"; do
	case $arg in
		"help"|"-help"|"--help")
			info "build script usage:"
			echo ""
			echo "build mode options:"
			echo "	<default>          builds this project in release mode"
			echo "	opt                builds this project in release mode + additional optimizations that take longer to compile (lto)"
			echo "	debug              builds this project in debug mode"
			echo "	clean              cleans all build binaries and intermediate build files"
			echo ""
			echo "build configuration:"
			echo "	no-opencl          disables opencl support"
			echo "	no-cuda            disables cuda support"
			echo "	no-host-compute    disables host compute support"
			echo "	no-metal           disables metal support (default for non-iOS and non-OS X targets)"
			echo "  no-vulkan          disables vulkan support"
			echo "	no-openal          disables openal support"
			echo "	no-net             disables network support"
			echo "	no-xml             disables xml support"
			echo "	no-exceptions      disables building with c++ exceptions"
			echo "	pocl               use the pocl library instead of the systems OpenCL library"
			echo "	libstdc++          use libstdc++ instead of libc++ (highly discouraged unless building on mingw)"
			echo "	x32                build a 32-bit binary "$(if [ "${BUILD_ARCH_SIZE}" == "x32" ]; then printf "(default on this platform)"; fi)
			echo "	x64                build a 64-bit binary "$(if [ "${BUILD_ARCH_SIZE}" == "x64" ]; then printf "(default on this platform)"; fi)
			echo ""
			echo "sanitizers:"
			echo "  asan               build with address sanitizer"
			echo "  msan               build with memory sanitizer"
			echo "  tsan               build with thread sanitizer"
			echo "  ubsan              build with undefined behavior sanitizer"
			echo ""
			echo "misc flags:"
			echo "	-v                 verbose output (prints all executed compiler and linker commands, and some other information)"
			echo "	-vv                very verbose output (same as -v + runs all compiler and linker commands with -v)"
			echo "	-j#                explicitly use # amount of build jobs (instead of automatically using #logical-cpus jobs)"
			echo ""
			echo ""
			echo "example:"
			echo "	./build.sh -v debug no-cuda no-openal -j1"
			echo ""
			exit 0
			;;
		"opt")
			BUILD_MODE="release_opt"
			;;
		"debug")
			BUILD_MODE="debug"
			;;
		"clean")
			BUILD_MODE="clean"
			;;
		"-v")
			BUILD_VERBOSE=1
			;;
		"-vv")
			BUILD_VERBOSE=2
			;;
		"-j"*)
			BUILD_JOB_COUNT=$(echo $arg | cut -c 3-)
			if [ -z ${BUILD_JOB_COUNT} ]; then
				BUILD_JOB_COUNT=0
			fi
			;;
		"no-opencl")
			BUILD_CONF_OPENCL=0
			;;
		"no-cuda")
			BUILD_CONF_CUDA=0
			;;
		"no-host-compute")
			BUILD_CONF_HOST_COMPUTE=0
			;;
		"no-metal")
			BUILD_CONF_METAL=0
			;;
		"no-vulkan")
			BUILD_CONF_VULKAN=0
			;;
		"no-openal")
			BUILD_CONF_OPENAL=0
			;;
		"no-net")
			BUILD_CONF_NET=0
			;;
		"no-xml")
			BUILD_CONF_XML=0
			;;
		"no-exceptions")
			BUILD_CONF_EXCEPTIONS=0
			;;
		"pocl")
			BUILD_CONF_POCL=1
			;;
		"libstdc++")
			BUILD_CONF_LIBSTDCXX=1
			;;
		"x32")
			BUILD_ARCH_SIZE="x32"
			;;
		"x64")
			BUILD_ARCH_SIZE="x64"
			;;
		"asan")
			BUILD_CONF_SANITIZERS=1
			BUILD_CONF_ASAN=1
			;;
		"msan")
			BUILD_CONF_SANITIZERS=1
			BUILD_CONF_MSAN=1
			;;
		"tsan")
			BUILD_CONF_SANITIZERS=1
			BUILD_CONF_TSAN=1
			;;
		"ubsan")
			BUILD_CONF_SANITIZERS=1
			BUILD_CONF_UBSAN=1
			;;
		*)
			warning "unknown argument: ${arg}"
			;;
	esac
done

##########################################
# target and build environment setup

# name of the target (part of the binary name)
TARGET_NAME=floor

# check on which platform we're compiling + check how many h/w threads can be used (logical cpus)
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
		;;
	"linux")
		BUILD_OS="linux"
		# note that this includes hyper-threading and multi-socket systems
		BUILD_CPU_COUNT=$(cat /proc/cpuinfo | grep "processor" | wc -l)
		;;
	"freebsd")
		BUILD_OS="freebsd"
		BUILD_CPU_COUNT=$(sysctl -n hw.ncpu)
		;;
	"openbsd")
		BUILD_OS="openbsd"
		BUILD_CPU_COUNT=$(sysctl -n hw.ncpu)
		;;
	"cygwin"*)
		# untested
		BUILD_OS="cygwin"
		BUILD_CPU_COUNT=$(env | grep 'NUMBER_OF_PROCESSORS' | sed -E 's/.*=([:digit:]*)/\1/g')
		warning "cygwin support is untested and unsupported!"
		;;
	"mingw"*)
		BUILD_OS="mingw"
		BUILD_CPU_COUNT=$(env | grep 'NUMBER_OF_PROCESSORS' | sed -E 's/.*=([:digit:]*)/\1/g')
		;;
	*)
		warning "unknown build platform - trying to continue! ${BUILD_PLATFORM}"
		;;
esac

# if an explicit job count was specified, overwrite BUILD_CPU_COUNT with it
if [ ${BUILD_JOB_COUNT} -gt 0 ]; then
	BUILD_CPU_COUNT=${BUILD_JOB_COUNT}
fi

# set the target binary name (depends on the platform)
TARGET_BIN_NAME=lib${TARGET_NAME}
# append 'd' for debug builds
if [ $BUILD_MODE == "debug" ]; then
	TARGET_BIN_NAME=${TARGET_BIN_NAME}d
fi

# use *.a for all platforms
TARGET_STATIC_BIN_NAME=${TARGET_BIN_NAME}_static.a

# file ending, depending on the platform we're building on
# osx/ios -> .dylib
if [ $BUILD_OS == "osx" -o $BUILD_OS == "ios" ]; then
	TARGET_BIN_NAME=${TARGET_BIN_NAME}.dylib
# windows/mingw/cygwin -> .dll
elif [ $BUILD_OS == "mingw" -o $BUILD_OS == "cygwin" ]; then
	TARGET_BIN_NAME=${TARGET_BIN_NAME}.dll
# else -> .so
else
	# default to .so for all other platforms (linux/*bsd/unknown)
	TARGET_BIN_NAME=${TARGET_BIN_NAME}.so
fi

# disable metal support on non-iOS/-OS X targets
if [ $BUILD_OS != "osx" -a $BUILD_OS != "ios" ]; then
	BUILD_CONF_METAL=0
fi

##########################################
# directory setup
# note that all paths are relative

# binary/library directory where the final binaries will be stored (*.so, *.dylib, etc.)
BIN_DIR=bin

# location of the target binary
TARGET_BIN=${BIN_DIR}/${TARGET_BIN_NAME}
TARGET_STATIC_BIN=${BIN_DIR}/${TARGET_STATIC_BIN_NAME}

# root folder of the source code
SRC_DIR=.

# all source code sub-directories, relative to SRC_DIR
SRC_SUB_DIRS="audio compute compute/cuda compute/host compute/metal compute/opencl compute/vulkan constexpr core floor lang math net threading"
if [ $BUILD_OS == "osx" -o $BUILD_OS == "ios" ]; then
	SRC_SUB_DIRS="${SRC_SUB_DIRS} darwin"
fi

# build directory where all temporary files are stored (*.o, etc.)
BUILD_DIR=build

##########################################
# library/dependency handling

# initial linker, lib and include setup
LDFLAGS="${LDFLAGS} -fvisibility=default"
if [ ${BUILD_CONF_LIBSTDCXX} -gt 0 ]; then
	LDFLAGS="${LDFLAGS} -stdlib=libstdc++"
else
	LDFLAGS="${LDFLAGS} -stdlib=libc++"
fi
LIBS="${LIBS}"
INCLUDES="${INCLUDES} -isystem /usr/local/include/c++/v1 -isystem /usr/include/c++/v1"
COMMON_FLAGS="${COMMON_FLAGS}"

# if no AR is specified, set it to the default ar (used when creating a static lib)
if [ -z "${AR}" ]; then
	AR=ar
fi

# set the correct 32/64-bit linker flag (use the default on mingw)
if [ $BUILD_OS != "mingw" ]; then
	if [ ${BUILD_ARCH_SIZE} == "x32" ]; then
		LDFLAGS="${LDFLAGS} -m32"
	else
		LDFLAGS="${LDFLAGS} -m64"
	fi
fi

# handle clang/llvm sanitizers
if [ ${BUILD_CONF_SANITIZERS} -gt 0 ]; then
	if [ ${BUILD_CONF_ASAN} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -fsanitize=address"
		COMMON_FLAGS="${COMMON_FLAGS} -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ ${BUILD_CONF_MSAN} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -fsanitize=memory"
		COMMON_FLAGS="${COMMON_FLAGS} -fsanitize=memory -fno-omit-frame-pointer"
	fi
	if [ ${BUILD_CONF_TSAN} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -fsanitize=thread"
		COMMON_FLAGS="${COMMON_FLAGS} -fsanitize=thread -fno-omit-frame-pointer"
	fi
	if [ ${BUILD_CONF_UBSAN} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -fsanitize=undefined-trap -fsanitize-undefined-trap-on-error"
		COMMON_FLAGS="${COMMON_FLAGS} -fsanitize=undefined-trap -fsanitize-undefined-trap-on-error -fno-omit-frame-pointer"
	fi
fi

# use pkg-config (and some manual libs/includes) on all platforms except osx/ios
if [ $BUILD_OS != "osx" -a $BUILD_OS != "ios" ]; then
	# build a shared library
	LDFLAGS="${LDFLAGS} -shared"
	
	# use PIC
	LDFLAGS="${LDFLAGS} -fPIC"
	COMMON_FLAGS="${COMMON_FLAGS} -fPIC"
	
	# pkg-config: required libraries/packages and optional libraries/packages
	PACKAGES="sdl2"
	PACKAGES_OPT=""
	if [ ${BUILD_CONF_XML} -gt 0 ]; then
		PACKAGES_OPT="${PACKAGES_OPT} libxml-2.0"
	fi
	if [ ${BUILD_CONF_NET} -gt 0 ]; then
		PACKAGES_OPT="${PACKAGES_OPT} libcrypto libssl"
	fi
	if [ ${BUILD_CONF_OPENAL} -gt 0 ]; then
		PACKAGES_OPT="${PACKAGES_OPT} openal"
	fi
	if [ ${BUILD_CONF_POCL} -gt 0 ]; then
		PACKAGES_OPT="${PACKAGES_OPT} pocl"
	fi

	# TODO: error checking + check if libs exist
	for pkg in ${PACKAGES}; do
		LIBS="${LIBS} $(pkg-config --libs "${pkg}")"
		COMMON_FLAGS="${COMMON_FLAGS} $(pkg-config --cflags "${pkg}")"
	done
	for pkg in ${PACKAGES_OPT}; do
		LIBS="${LIBS} $(pkg-config --libs "${pkg}")"
		COMMON_FLAGS="${COMMON_FLAGS} $(pkg-config --cflags "${pkg}")"
	done

	# libs that don't have pkg-config
	UNCHECKED_LIBS=""
	if [ $BUILD_OS != "mingw" ]; then
		# must link pthread on unix
		UNCHECKED_LIBS="${UNCHECKED_LIBS} pthread"
	else
		# must link winpthread instead on windows/mingw
		UNCHECKED_LIBS="${UNCHECKED_LIBS} winpthread"
	fi
	if [ ${BUILD_CONF_OPENCL} -gt 0 -a ${BUILD_CONF_POCL} -eq 0 ]; then
		UNCHECKED_LIBS="${UNCHECKED_LIBS} OpenCL"
	fi
	if [ ${BUILD_CONF_VULKAN} -gt 0 ]; then
		UNCHECKED_LIBS="${UNCHECKED_LIBS} vulkan"
	fi

	# add os specific libs
	if [ $BUILD_OS == "linux" -o $BUILD_OS == "freebsd" -o $BUILD_OS == "openbsd" ]; then
		UNCHECKED_LIBS="${UNCHECKED_LIBS} GL Xxf86vm"
	elif [ $BUILD_OS == "mingw" -o $BUILD_OS == "cygwin" ]; then
		UNCHECKED_LIBS="${UNCHECKED_LIBS} opengl32 glu32 gdi32 ws2_32"
	fi
	
	# linux:
	#  * must also link against c++abi when using libc++
	#  * need to add the /lib folder
	if [ $BUILD_OS == "linux" ]; then
		if [ ${BUILD_CONF_LIBSTDCXX} -eq 0 ]; then
			UNCHECKED_LIBS="${UNCHECKED_LIBS} c++abi"
		fi
		LDFLAGS="${LDFLAGS} -L/lib"
	fi
	
	# windows/mingw opencl handling
	if [ $BUILD_OS == "mingw" ]; then
		if [ ${BUILD_CONF_OPENCL} -gt 0 ]; then
			if [ ! -z "${AMDAPPSDKROOT}" ]; then
				# use amd opencl sdk
				AMDAPPSDKROOT_FIXED=$(echo ${AMDAPPSDKROOT} | sed -E "s/\\\\/\//g")
				if [ ${BUILD_ARCH_SIZE} == "x32" ]; then
					LDFLAGS="${LDFLAGS} -L\"${AMDAPPSDKROOT_FIXED}lib/x86\""
				else
					LDFLAGS="${LDFLAGS} -L\"${AMDAPPSDKROOT_FIXED}lib/x86_64\""
				fi
				INCLUDES="${INCLUDES} -isystem \"${AMDAPPSDKROOT_FIXED}include\""
			elif [ ! -z "${INTELOCLSDKROOT}" ]; then
				# use intel opencl sdk
				INTELOCLSDKROOT_FIXED=$(echo ${INTELOCLSDKROOT} | sed -E "s/\\\\/\//g")
				if [ ${BUILD_ARCH_SIZE} == "x32" ]; then
					LDFLAGS="${LDFLAGS} -L\"${INTELOCLSDKROOT_FIXED}lib/x86\""
				else
					LDFLAGS="${LDFLAGS} -L\"${INTELOCLSDKROOT_FIXED}lib/x64\""
				fi
				INCLUDES="${INCLUDES} -isystem \"${INTELOCLSDKROOT_FIXED}include\""
			else
				error "building with OpenCL support, but no OpenCL SDK was found - please install the Intel or AMD OpenCL SDK!"
			fi
		fi
		# TODO: vulkan?
	fi

	for lib in ${UNCHECKED_LIBS}; do
		LIBS="${LIBS} -l${lib}"
	done
	
	# mingw: "--allow-multiple-definition" is necessary, because gcc is still used as a linker
	# and will always link against libstdc++/libsupc++ (-> multiple definitions with libc++)
	# also note: since libc++ is linked first, libc++'s functions will be used
	if [ $BUILD_OS == "mingw" -a ${BUILD_CONF_LIBSTDCXX} -eq 0 ]; then
		LDFLAGS="${LDFLAGS} -lc++.dll -Wl,--allow-multiple-definition -lsupc++"
	fi
	
	# needed for ___chkstk_ms
	if [ $BUILD_OS == "mingw" ]; then
		LDFLAGS="${LDFLAGS} -lgcc"
	fi
	
	# add all libs to LDFLAGS
	LDFLAGS="${LDFLAGS} ${LIBS}"
else
	# on osx/ios: assume everything is installed, pkg-config doesn't really exist
	INCLUDES="${INCLUDES} -isystem /opt/X11/include"
	if [ ${BUILD_CONF_XML} -gt 0 ]; then
		INCLUDES="${INCLUDES} -isystem /usr/include/libxml2"
	fi
	if [ ${BUILD_CONF_NET} -gt 0 ]; then
		INCLUDES="${INCLUDES} -isystem /usr/local/opt/openssl/include"
	fi
	INCLUDES="${INCLUDES} -iframework /Library/Frameworks"
	
	# build a shared/dynamic library
	LDFLAGS="${LDFLAGS} -dynamiclib"
	
	# additional lib paths
	LDFLAGS="${LDFLAGS} -L/opt/X11/lib"
	if [ ${BUILD_CONF_NET} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -L/usr/local/opt/openssl/lib"
	fi

	# rpath voodoo
	LDFLAGS="${LDFLAGS} -install_name @rpath/${TARGET_BIN_NAME}"
	LDFLAGS="${LDFLAGS} -Xlinker -rpath -Xlinker @loader_path/../Resources"
	LDFLAGS="${LDFLAGS} -Xlinker -rpath -Xlinker @loader_path/../Frameworks"
	LDFLAGS="${LDFLAGS} -Xlinker -rpath -Xlinker /Library/Frameworks"
	LDFLAGS="${LDFLAGS} -Xlinker -rpath -Xlinker /usr/local/lib"
	LDFLAGS="${LDFLAGS} -Xlinker -rpath -Xlinker /usr/lib"
	LDFLAGS="${LDFLAGS} -Xlinker -rpath -Xlinker /opt/floor/lib"
	
	# probably necessary
	LDFLAGS="${LDFLAGS} -fobjc-link-runtime"
	
	# frameworks and libs
	LDFLAGS="${LDFLAGS} -framework SDL2"
	if [ ${BUILD_CONF_XML} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -lxml2"
	fi
	if [ ${BUILD_CONF_NET} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -lcrypto -lssl"
	fi
	if [ ${BUILD_CONF_OPENAL} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -framework OpenALSoft"
	fi
	
	# system frameworks
	LDFLAGS="${LDFLAGS} -framework ApplicationServices -framework AppKit -framework Cocoa -framework OpenGL -framework QuartzCore"
	if [ ${BUILD_CONF_OPENCL} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -framework OpenCL"
	fi
	if [ ${BUILD_CONF_METAL} -gt 0 ]; then
		LDFLAGS="${LDFLAGS} -framework Metal"
	fi
fi

# just in case, also add these rather default ones (should also go after all previous libs/includes,
# in case a local or otherwise set up lib is overwriting a system lib and should be used instead)
LDFLAGS="${LDFLAGS} -L/usr/lib -L/usr/local/lib -L/opt/floor/lib"
INCLUDES="${INCLUDES}"
# don't automatically add /usr/include and /usr/local/include on mingw/msys (these will lead to the wrong headers being included)
if [ $BUILD_OS != "mingw" ]; then
    INCLUDES="${INCLUDES} -isystem /usr/include -isystem /usr/local/include"
fi

# create the floor_conf.hpp file
CONF=$(cat floor/floor_conf.hpp.in)
set_conf_val() {
	repl_text="$1"
	define="$2"
	enabled=$3
	if [ ${enabled} -gt 0 ]; then
		CONF=$(echo "${CONF}" | sed -E "s/${repl_text}/\/\/#define ${define} 1/g")
	else
		CONF=$(echo "${CONF}" | sed -E "s/${repl_text}/#define ${define} 1/g")
	fi
}
set_conf_val "###FLOOR_CUDA###" "FLOOR_NO_CUDA" ${BUILD_CONF_CUDA}
set_conf_val "###FLOOR_OPENCL###" "FLOOR_NO_OPENCL" ${BUILD_CONF_OPENCL}
set_conf_val "###FLOOR_HOST_COMPUTE###" "FLOOR_NO_HOST_COMPUTE" ${BUILD_CONF_HOST_COMPUTE}
set_conf_val "###FLOOR_VULKAN###" "FLOOR_NO_VULKAN" ${BUILD_CONF_VULKAN}
# NOTE: metal is disabled on non-ios platforms anyways and this would overwrite the ios flag that is already ifdef'ed
if [ $BUILD_OS != "ios" ]; then
	set_conf_val "###FLOOR_METAL###" "FLOOR_NO_METAL" 1
else
	# -> only actually set the config option if we're building on ios
	set_conf_val "###FLOOR_METAL###" "FLOOR_NO_METAL" ${BUILD_CONF_METAL}
fi
set_conf_val "###FLOOR_OPENAL###" "FLOOR_NO_OPENAL" ${BUILD_CONF_OPENAL}
set_conf_val "###FLOOR_NET###" "FLOOR_NO_NET" ${BUILD_CONF_NET}
set_conf_val "###FLOOR_XML###" "FLOOR_NO_XML" ${BUILD_CONF_XML}
set_conf_val "###FLOOR_EXCEPTIONS###" "FLOOR_NO_EXCEPTIONS" ${BUILD_CONF_EXCEPTIONS}
echo "${CONF}" > floor/floor_conf.hpp

# only update build version if FLOOR_DEV environment variable is set
if [ -n "${FLOOR_DEV}" ]; then
	# checks if any source files have updated (are newer than the target binary)
	# if so, this increments the build version by one (updates the header file)
	info "build version update ..."
	. ./floor/build_version.sh
fi

# version of the target (preprocess the floor version header, grep the version defines, transform them to exports and eval)
eval $(${CXX} -E -dM -I../ floor/floor_version.hpp 2>&1 | grep -E "define (FLOOR_(MAJOR|MINOR|REVISION|DEV_STAGE|BUILD)_VERSION|FLOOR_DEV_STAGE_VERSION_STR) " | sed -E "s/.*define (.*) [\"]*([^ \"]*)[\"]*/export \1=\2/g")
TARGET_VERSION="${FLOOR_MAJOR_VERSION}.${FLOOR_MINOR_VERSION}.${FLOOR_REVISION_VERSION}"
TARGET_FULL_VERSION="${TARGET_VERSION}${FLOOR_DEV_STAGE_VERSION_STR}-${FLOOR_BUILD_VERSION}"
info "building ${TARGET_NAME} v${TARGET_FULL_VERSION} (${BUILD_MODE})"

##########################################
# flags

# set up initial c++ and c flags
CXXFLAGS="${CXXFLAGS} -std=gnu++14"
if [ ${BUILD_CONF_LIBSTDCXX} -gt 0 ]; then
	CXXFLAGS="${CXXFLAGS} -stdlib=libstdc++"
else
	CXXFLAGS="${CXXFLAGS} -stdlib=libc++"
fi
CFLAGS="${CFLAGS} -std=gnu11"

OBJCFLAGS="${OBJCFLAGS} -fno-objc-exceptions"
if [ $BUILD_OS == "osx" -o $BUILD_OS == "ios" ]; then
	OBJCFLAGS="${OBJCFLAGS} -fobjc-arc"
fi

# disable exception support
if [ ${BUILD_CONF_EXCEPTIONS} -eq 0 ]; then
	CXXFLAGS="${CXXFLAGS} -fno-exceptions"
fi

# so not standard compliant ...
if [ $BUILD_OS == "mingw" ]; then
	CXXFLAGS="${CXXFLAGS} -pthread"
fi

# arch handling (use -arch on osx/ios and -m32/-m64 everywhere else, except for mingw)
if [ $BUILD_OS == "osx" -o $BUILD_OS == "ios" ]; then
	if [ ${BUILD_ARCH_SIZE} == "x32" ]; then
		case $BUILD_ARCH in
			"i386"|"i486"|"i586"|"i686")
				COMMON_FLAGS="${COMMON_FLAGS} -arch $BUILD_ARCH"
				;;
			"x86_64"|"amd64")
				COMMON_FLAGS="${COMMON_FLAGS} -arch i686"
				;;
			"arm"*)
				COMMON_FLAGS="${COMMON_FLAGS} -arch armv7"
				;;
			*)
				warning "unknown arch (${BUILD_ARCH}) - building for arch i686!"
				COMMON_FLAGS="${COMMON_FLAGS} -arch i686"
				;;
		esac
	else
		case $BUILD_ARCH in
			"arm"*)
				COMMON_FLAGS="${COMMON_FLAGS} -arch arm64"
				;;
			*)
				COMMON_FLAGS="${COMMON_FLAGS} -arch x86_64"
				;;
		esac
	fi
elif [ $BUILD_OS != "mingw" ]; then
	# NOTE: mingw will/should/has to use the compiler default
	if [ ${BUILD_ARCH_SIZE} == "x32" ]; then
		COMMON_FLAGS="${COMMON_FLAGS} -m32"
	else
		COMMON_FLAGS="${COMMON_FLAGS} -m64"
	fi
fi

# c++ and c flags that apply to all build configurations
COMMON_FLAGS="${COMMON_FLAGS} -ffast-math -fstrict-aliasing"

# debug flags, only used in the debug target
DEBUG_FLAGS="-O0 -DFLOOR_DEBUG=1 -DDEBUG"
if [ $BUILD_OS != "mingw" ]; then
	DEBUG_FLAGS="${DEBUG_FLAGS} -gdwarf-2"
else
	DEBUG_FLAGS="${DEBUG_FLAGS} -g"
fi

# release mode flags/optimizations
# TODO: sse/avx selection/config? default to sse4.1 for now (core2)
# TODO: also add -mtune option
REL_FLAGS="-Ofast -funroll-loops -msse4.1"

# additional optimizations (used in addition to REL_CXX_FLAGS)
REL_OPT_FLAGS="-flto"
REL_OPT_LD_FLAGS="-flto"

# osx/ios: set min version
if [ $BUILD_OS == "osx" -o $BUILD_OS == "ios" ]; then
	if [ $BUILD_OS == "osx" ]; then
		COMMON_FLAGS="${COMMON_FLAGS} -mmacosx-version-min=10.9"
	else # ios
		if [ ${BUILD_CONF_METAL} -gt 0 ]; then
			COMMON_FLAGS="${COMMON_FLAGS} -miphoneos-version-min=9.0"
		else
			COMMON_FLAGS="${COMMON_FLAGS} -miphoneos-version-min=7.0"
		fi
	fi
	
	# set lib version
	LDFLAGS="${LDFLAGS} -compatibility_version ${TARGET_VERSION} -current_version ${TARGET_VERSION}"
fi

# defines:
# set platform size define
if [ ${BUILD_ARCH_SIZE} == "x32" ]; then
	COMMON_FLAGS="${COMMON_FLAGS} -DPLATFORM_X32"
else
	COMMON_FLAGS="${COMMON_FLAGS} -DPLATFORM_X64"
fi
if [ $BUILD_OS == "mingw" -o $BUILD_OS == "cygwin" ]; then
	# common windows "unix environment" flag
	COMMON_FLAGS="${COMMON_FLAGS} -DWIN_UNIXENV"
	if [ $BUILD_OS == "mingw" ]; then
		# set __WINDOWS__ and mingw specific flag
		COMMON_FLAGS="${COMMON_FLAGS} -D__WINDOWS__ -DMINGW"
	fi
	if [ $BUILD_OS == "cygwin" ]; then
		# set cygwin specific flag
		COMMON_FLAGS="${COMMON_FLAGS} -DCYGWIN"
	fi
fi

# hard-mode c++ ;) TODO: clean this up + explanations
WARNINGS="${WARNINGS} -Weverything -Wthread-safety -Wthread-safety-negative -Wthread-safety-beta -Wthread-safety-verbose"
WARNINGS="${WARNINGS} -Wno-gnu -Wno-c++98-compat"
WARNINGS="${WARNINGS} -Wno-c++98-compat-pedantic -Wno-c99-extensions"
WARNINGS="${WARNINGS} -Wno-header-hygiene -Wno-documentation"
WARNINGS="${WARNINGS} -Wno-system-headers -Wno-global-constructors -Wno-padded"
WARNINGS="${WARNINGS} -Wno-packed -Wno-switch-enum -Wno-exit-time-destructors"
WARNINGS="${WARNINGS} -Wno-unknown-warning-option -Wno-nested-anon-types"
WARNINGS="${WARNINGS} -Wno-old-style-cast -Wno-date-time -Wno-reserved-id-macro"
WARNINGS="${WARNINGS} -Wno-documentation-unknown-command -Wno-partial-availability"
if [ ${BUILD_ARCH_SIZE} == "x32" ]; then
	# ignore warnings about required alignment increases on 32-bit platforms (won't and can't fix)
	WARNINGS="${WARNINGS} -Wno-cast-align"
fi
COMMON_FLAGS="${COMMON_FLAGS} ${WARNINGS}"

# diagnostics
COMMON_FLAGS="${COMMON_FLAGS} -fdiagnostics-show-note-include-stack -fmessage-length=0 -fmacro-backtrace-limit=0"
COMMON_FLAGS="${COMMON_FLAGS} -fparse-all-comments -fno-elide-type -fdiagnostics-show-template-tree"

# includes + replace all "-I"s with "-isystem"s so that we don't get warnings in external headers
COMMON_FLAGS="${COMMON_FLAGS} ${INCLUDES}"
COMMON_FLAGS=$(echo "${COMMON_FLAGS}" | sed -E "s/-I/-isystem /g")
COMMON_FLAGS="${COMMON_FLAGS} -I../"

# mingw sdl fixes
if [ $BUILD_OS == "mingw" ]; then
	# remove sdls main redirect, we want to use our own main
	COMMON_FLAGS=$(echo "${COMMON_FLAGS}" | sed -E "s/-Dmain=SDL_main//g")
	# remove windows flag -> creates a separate cmd window + working iostream output
	LDFLAGS=$(echo "${LDFLAGS}" | sed -E "s/-mwindows //g")
	# remove unwanted -static-libgcc flag (this won't work and lead to linker errors!)
	LDFLAGS=$(echo "${LDFLAGS}" | sed -E "s/-static-libgcc //g")
	# remove unwanted -lm (this won't work and lead to linker errors!)
	LDFLAGS=$(echo "${LDFLAGS}" | sed -E "s/-lm //g")
fi

# finally: add all common c++ and c flags/options
CXXFLAGS="${CXXFLAGS} ${COMMON_FLAGS}"
CFLAGS="${CFLAGS} ${COMMON_FLAGS}"

##########################################
# targets and building

# get all source files (c++/c/objective-c++/objective-c) and create build folders
for dir in ${SRC_SUB_DIRS}; do
	# source files
	SRC_FILES="${SRC_FILES} $(find ${dir} -maxdepth 1 -type f -name '*.cpp' | grep -v "\._")"
	SRC_FILES="${SRC_FILES} $(find ${dir} -maxdepth 1 -type f -name '*.c' | grep -v "\._")"
	SRC_FILES="${SRC_FILES} $(find ${dir} -maxdepth 1 -type f -name '*.mm' | grep -v "\._")"
	SRC_FILES="${SRC_FILES} $(find ${dir} -maxdepth 1 -type f -name '*.m' | grep -v "\._")"
	
	# create resp. build folder
	mkdir -p ${BUILD_DIR}/${dir}
done

# total hack, but it makes building faster: reverse the source file order, so that math and net files
# are compiled first (these take longest to compile, so pipeline them properly)
REV_SRC_FILES=""
for source_file in ${SRC_FILES}; do
	REV_SRC_FILES="${source_file} ${REV_SRC_FILES}"
done
SRC_FILES=${REV_SRC_FILES}

# make a list of all object files
for source_file in ${SRC_FILES}; do
	OBJ_FILES="${OBJ_FILES} ${BUILD_DIR}/${source_file}.o"
done

# set flags depending on the build mode, or make a clean exit
case ${BUILD_MODE} in
	"release")
		# release mode (default): add release mode flags/optimizations
		CXXFLAGS="${CXXFLAGS} ${REL_FLAGS}"
		CFLAGS="${CFLAGS} ${REL_FLAGS}"
		;;
	"release_opt")
		# release mode + additional optimizations: add release mode and opt flags
		CXXFLAGS="${CXXFLAGS} ${REL_FLAGS} ${REL_OPT_FLAGS}"
		CFLAGS="${CFLAGS} ${REL_FLAGS} ${REL_OPT_FLAGS}"
		LDFLAGS="${LDFLAGS} ${REL_OPT_LD_FLAGS}"
		;;
	"debug")
		# debug mode: add debug flags
		CXXFLAGS="${CXXFLAGS} ${DEBUG_FLAGS}"
		CFLAGS="${CFLAGS} ${DEBUG_FLAGS}"
		;;
	"clean")
		# delete the target binary and the complete build folder (all object files)
		info "cleaning ..."
		rm -f ${TARGET_BIN}
		rm -Rf ${BUILD_DIR}
		exit 0
		;;
	*)
		error "unknown build mode: ${BUILD_MODE}"
		;;
esac

if [ ${BUILD_VERBOSE} -gt 1 ]; then
	CXXFLAGS="${CXXFLAGS} -v"
	CFLAGS="${CFLAGS} -v"
	LDFLAGS="${LDFLAGS} -v"
fi
if [ ${BUILD_VERBOSE} -gt 0 ]; then
	info ""
	info "using CXXFLAGS: ${CXXFLAGS}"
	info ""
	info "using CFLAGS: ${CFLAGS}"
	info ""
	info "using LDFLAGS: ${LDFLAGS}"
	info ""
fi

# build the precompiled header
# -> kill old pch file if it exists
if [ -f "floor.pch" ]; then
	rm floor.pch
fi
info "building precompiled header ..."
precomp_header_cmd="${CXX} ${CXXFLAGS} -x c++-header floor_prefix.pch -Xclang -emit-pch -o floor.pch"
verbose "${precomp_header_cmd}"
eval ${precomp_header_cmd}

if [ ! -f "floor.pch" ]; then
	error "precompiled header compilation failed"
fi

# build the target
export build_error=false
trap build_error=true USR1
build_file() {
	# this function builds one source file
	source_file=$1
	file_num=$2
	file_count=$3
	parent_pid=$4
	info "building ${source_file} [${file_num}/${file_count}]"
	case ${source_file} in
		*".cpp")
			build_cmd="${CXX} -include-pch floor.pch ${CXXFLAGS}"
			;;
		*".c")
			build_cmd="${CC} ${CFLAGS}"
			;;
		*".mm")
			build_cmd="${CXX} -x objective-c++ ${OBJCFLAGS} ${CXXFLAGS}"
			;;
		*".m")
			build_cmd="${CC} -x objective-c ${OBJCFLAGS} ${CFLAGS}"
			;;
		*)
			error "unknown source file ending: ${source_file}"
			;;
	esac
	build_cmd="${build_cmd} -c ${source_file} -o ${BUILD_DIR}/${source_file}.o -MMD -MT deps -MF ${BUILD_DIR}/${source_file}.d"
	verbose "${build_cmd}"
	eval ${build_cmd}

	# handle errors
	ret_code=$?
	if [ ${ret_code} -ne 0 ]; then
		kill -USR1 ${parent_pid}
		error "compilation failed (${source_file})"
	fi
}
job_count() {
	echo $(jobs -p | wc -l)
}

# get the amount of source files and create a counter (this is used for some info/debug output)
file_count=$(echo "${SRC_FILES}" | wc -w | tr -d [:space:])
file_counter=0
# iterate over all source files and create a build job for each of them
for source_file in ${SRC_FILES}; do
	file_counter=$(expr $file_counter + 1)
	
	# if only one build job should be used, don't bother with shell jobs
	# this also works around an issue where "jobs -p" always lists one job even after it has finished
	if [ $BUILD_CPU_COUNT -gt 1 ]; then
		# make sure that there are only $BUILD_CPU_COUNT active jobs at any time,
		# this should be the most efficient setup for concurrently building multiple files
		while true; do
			cur_job_count=$(job_count)
			if [ $cur_job_count -lt $BUILD_CPU_COUNT ]; then
				break
			fi
			sleep 0.1
		done
		(build_file $source_file $file_counter $file_count $$) &
	else
		build_file $source_file $file_counter $file_count $$
	fi

	# abort on build errors
	if [ ${build_error} == "true" ]; then
		# wait until all build jobs have finished (all error output has been written)
		wait
		exit -1
	fi
done
# all jobs were started, now we just have to wait until all are done
sleep 0.1
info "waiting for build jobs to finish ..."
wait

# link
info "linking ..."
mkdir -p ${BIN_DIR}

linker_cmd="${CXX} -o ${TARGET_BIN} ${OBJ_FILES} ${LDFLAGS}"
verbose "${linker_cmd}"
eval ${linker_cmd}

verbose "${AR} rs ${TARGET_STATIC_BIN} ${OBJ_FILES}"
${AR} rs ${TARGET_STATIC_BIN} ${OBJ_FILES}

info "built ${TARGET_NAME} v${TARGET_FULL_VERSION}"
