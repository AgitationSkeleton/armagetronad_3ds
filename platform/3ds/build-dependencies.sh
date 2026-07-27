#!/bin/sh
# Builds the vendored dependencies for the 3DS, and generates the protobuf
# sources the client needs.
#
# devkitPro's portlibs cover SDL, freetype, libpng, curl and the audio codecs,
# but not protobuf, libxml2 or FTGL, so those are vendored under vendor/ and
# cross built here. Each step is skipped when its output already exists, so
# this is cheap to re-run and friendly to a CI cache.
#
#   DEVKITPRO   defaults to /opt/devkitpro
#   JOBS        parallelism, defaults to the number of processors

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
vendor="$here/vendor"

: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}

toolchain="$DEVKITPRO/cmake/3DS.cmake"
[ -f "$toolchain" ] || { echo "3DS cmake toolchain not found at $toolchain" >&2; exit 1; }

echo "==> libxml2"
if [ ! -f "$vendor/libxml2-build-3ds/libxml2.a" ]; then
    # The client only parses local XML: resource files, cockpits and the like.
    # Everything that would reach the network or the filesystem on its own is
    # off, which also drops the dependencies those bring in.
    cmake -S "$vendor/libxml2" -B "$vendor/libxml2-build-3ds" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DLIBXML2_WITH_FTP=OFF \
        -DLIBXML2_WITH_HTTP=OFF \
        -DLIBXML2_WITH_ICONV=OFF \
        -DLIBXML2_WITH_ICU=OFF \
        -DLIBXML2_WITH_LZMA=OFF \
        -DLIBXML2_WITH_MODULES=OFF \
        -DLIBXML2_WITH_PROGRAMS=OFF \
        -DLIBXML2_WITH_PYTHON=OFF \
        -DLIBXML2_WITH_TESTS=OFF \
        -DLIBXML2_WITH_ZLIB=OFF
    cmake --build "$vendor/libxml2-build-3ds" -j "$JOBS"
fi

echo "==> protobuf runtime"
if [ ! -f "$vendor/protobuf-build-3ds/libprotobuf.a" ]; then
    # Only the runtime. protoc itself is a host tool and is built separately
    # below; asking for it here would try to run ARM binaries on the builder.
    cmake -S "$vendor/protobuf" -B "$vendor/protobuf-build-3ds" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_BUILD_CONFORMANCE=OFF \
        -Dprotobuf_BUILD_EXAMPLES=OFF \
        -Dprotobuf_BUILD_LIBPROTOC=OFF \
        -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
        -Dprotobuf_INSTALL=OFF \
        -Dprotobuf_WITH_ZLIB=OFF
    cmake --build "$vendor/protobuf-build-3ds" -j "$JOBS"
fi

echo "==> FTGL"
if [ ! -f "$vendor/ftgl-build-3ds/libftgl.a" ]; then
    make -C "$vendor/ftgl-build-3ds" -j "$JOBS"
fi

echo "==> protoc (host)"
protoc="$vendor/protobuf-build-host/protoc"
if [ ! -x "$protoc" ]; then
    # Built from the same source as the runtime above. A protoc from the
    # distribution would be some other version, and generated code has to be
    # no newer than the runtime it is linked against.
    cmake -S "$vendor/protobuf" -B "$vendor/protobuf-build-host" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_BUILD_CONFORMANCE=OFF \
        -Dprotobuf_BUILD_EXAMPLES=OFF \
        -Dprotobuf_BUILD_PROTOC_BINARIES=ON \
        -Dprotobuf_INSTALL=OFF \
        -Dprotobuf_WITH_ZLIB=OFF
    cmake --build "$vendor/protobuf-build-host" -j "$JOBS" --target protoc
fi

echo "==> protobuf sources"
mkdir -p "$here/generated"
"$protoc" --proto_path="$root/src/protobuf" \
    --cpp_out="$here/generated" \
    "$root"/src/protobuf/*.proto
echo "generated $(find "$here/generated" -name '*.pb.cc' | wc -l) protobuf sources"
