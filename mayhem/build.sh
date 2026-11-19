#!/usr/bin/env bash
# mayhem/build.sh — builds the Millennium fuzz harnesses and the known-answer self-test.
#
# Millennium's full CMake build produces a Steam-injected shared library (Steam hooks, curl,
# a Rust staticlib via Corrosion, re2 fetched over git). The harnesses only need the parser
# translation units, so they are compiled directly (the way tests/cpp/CMakeLists.txt does),
# with the project sources instrumented by $SANITIZER_FLAGS. nlohmann_json comes from the
# tarball upstream vendors in thirdparty/, so the build is offline and re-runnable.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
cd "$SRC"

B="$SRC/mayhem/build"          # gitignored by upstream's **/build
mkdir -p "$B/obj" "$B/selftest"

# nlohmann_json 3.12.0 headers from upstream's vendored tarball (re-extracted every run).
rm -rf "$B/nlohmann"
mkdir -p "$B/nlohmann"
tar -xzf thirdparty/nlohmann_json-v3.12.0.tar.gz -C "$B/nlohmann" --strip-components=2 json-3.12.0/single_include/nlohmann
INC=(-I"$SRC/src" -I"$SRC/src/include" -I"$B/nlohmann" -DGIT_COMMIT_HASH='"mayhem"')

# LuaJIT 2.1 (upstream's fork in thirdparty/forks/luajit): the runtime the Lua plugin API runs
# in. A dependency, so it stays uninstrumented; built from a fresh copy so the tree stays clean.
rm -rf "$B/luajit"
cp -r thirdparty/forks/luajit "$B/luajit"
make -C "$B/luajit/src" -j"$MAYHEM_JOBS" CC="$CC" BUILDMODE=static XCFLAGS="-g -gdwarf-3" libluajit.a >/dev/null
LUA_INC=(-I"$B/luajit/src")
LUA_LIB="$B/luajit/src/libluajit.a"

# zlib-ng (zlib-compat) + minizip-ng from upstream's vendored tarballs, configured the way
# scripts/cmake/bootstrap_deps.cmake does. Dependencies of the zip target, so uninstrumented.
DEPS="$B/deps"
rm -rf "$B/zlib-ng" "$B/minizip-ng" "$DEPS"
mkdir -p "$B/zlib-ng" "$B/minizip-ng"
tar -xzf thirdparty/zlib-2.2.5.tar.gz -C "$B/zlib-ng" --strip-components=1
tar -xzf thirdparty/minizip_ng-4.0.10.tar.gz -C "$B/minizip-ng" --strip-components=1
CFLAGS="-g -gdwarf-3 -O2" cmake -S "$B/zlib-ng" -B "$B/zlib-ng/out" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF -DZLIB_COMPAT=ON -DZLIB_ENABLE_TESTS=OFF -DZLIBNG_ENABLE_TESTS=OFF \
  -DWITH_GTEST=OFF -DCMAKE_INSTALL_PREFIX="$DEPS" >/dev/null
cmake --build "$B/zlib-ng/out" -j"$MAYHEM_JOBS" --target install >/dev/null
CFLAGS="-g -gdwarf-3 -O2" cmake -S "$B/minizip-ng" -B "$B/minizip-ng/out" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF -DMZ_FETCH_LIBS=OFF -DMZ_ZLIB=ON -DMZ_BZIP2=OFF -DMZ_LZMA=OFF -DMZ_ZSTD=OFF \
  -DMZ_OPENSSL=OFF -DMZ_LIBBSD=OFF -DMZ_ICONV=OFF -DMZ_PKCRYPT=OFF -DMZ_WZAES=OFF -DMZ_LIBCOMP=OFF \
  -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_INSTALL_PREFIX="$DEPS" >/dev/null
cmake --build "$B/minizip-ng/out" -j"$MAYHEM_JOBS" --target install >/dev/null
MZ_INC=(-I"$DEPS/include/minizip" -I"$DEPS/include")
MZ_LIBS=("$DEPS/lib/libminizip.a" "$DEPS/lib/libz.a")

PROJECT_SRCS=(src/engine/star_parser.cc src/util/semver.cc src/util/cmdline_parser.cc src/bindings/css_parser.cc)

# 1) project sources + shared harness pieces, sanitized
# -fsanitize=fuzzer-no-link: SanCov edge instrumentation on the project objects (the link adds libFuzzer).
FUZZ_CXXFLAGS=(-std=c++23 -O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link)
objs=() pids=()
for s in "${PROJECT_SRCS[@]}" mayhem/harnesses/logger_stub.cc mayhem/lsan_off.cc; do
  o="$B/obj/$(basename "${s%.*}").o"
  "$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" -c "$s" -o "$o" & pids+=($!)
  objs+=("$o")
done
# Lua plugin API modules (project code, instrumented); json.c is built -w as upstream does
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1 -w "${LUA_INC[@]}" -c src/lua_host/api/json.c -o "$B/obj/lua_json.o" & pids+=($!)
"$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" "${LUA_INC[@]}" -c src/lua_host/api/utils.cc -o "$B/obj/lua_utils.o" & pids+=($!)
lua_objs=("$B/obj/lua_json.o" "$B/obj/lua_utils.o")
# loopback IPC reader (CEF hook helper -> Millennium) and the registries it touches
ipc_objs=()
for s in src/instrumentation/internal/loopback_ipc.cc src/instrumentation/internal/patch_registry.cc src/mep/patch_stream_recorder.cc; do
  o="$B/obj/ipc_$(basename "${s%.*}").o"
  "$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" -c "$s" -o "$o" & pids+=($!)
  ipc_objs+=("$o")
done
# plugin/theme/update archive extraction (src/util/zip.cc)
"$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" "${MZ_INC[@]}" -c src/util/zip.cc -o "$B/obj/zip.o" & pids+=($!)
# Lua host RPC client + the millennium/config Lua modules that decode the parent's replies
LH_INC=(-I"$SRC/src/lua_host/include")
rpc_objs=()
for s in src/lua_host/rpc.cc src/lua_host/api/millennium.cc src/lua_host/api/config.cc; do
  o="$B/obj/lh_$(basename "${s%.*}").o"
  "$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" "${LH_INC[@]}" "${LUA_INC[@]}" -c "$s" -o "$o" & pids+=($!)
  rpc_objs+=("$o")
done
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/obj/standalone_main.o" & pids+=($!)
for p in "${pids[@]}"; do wait "$p"; done
ipc_objs+=("$B/obj/logger_stub.o" "$B/obj/lsan_off.o")

# 2) each harness: libFuzzer binary + run-once reproducer
for h in fuzz_star_file fuzz_star_decompress fuzz_semver fuzz_cmdline fuzz_css_parser; do
  "$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" -c "mayhem/harnesses/$h.cc" -o "$B/obj/$h.o"
  "$CXX" "${FUZZ_CXXFLAGS[@]}" $LIB_FUZZING_ENGINE "$B/obj/$h.o" "${objs[@]}" -lcrypto -o "$SRC/$h"
  "$CXX" "${FUZZ_CXXFLAGS[@]}" "$B/obj/$h.o" "$B/obj/standalone_main.o" "${objs[@]}" -lcrypto -o "$SRC/$h-standalone"
done
h=fuzz_loopback_ipc
"$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" -c "mayhem/harnesses/$h.cc" -o "$B/obj/$h.o"
"$CXX" "${FUZZ_CXXFLAGS[@]}" $LIB_FUZZING_ENGINE "$B/obj/$h.o" "${ipc_objs[@]}" -pthread -o "$SRC/$h"
"$CXX" "${FUZZ_CXXFLAGS[@]}" "$B/obj/$h.o" "$B/obj/standalone_main.o" "${ipc_objs[@]}" -pthread -o "$SRC/$h-standalone"
h=fuzz_zip_extract
"$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" "${MZ_INC[@]}" -c "mayhem/harnesses/$h.cc" -o "$B/obj/$h.o"
"$CXX" "${FUZZ_CXXFLAGS[@]}" $LIB_FUZZING_ENGINE "$B/obj/$h.o" "$B/obj/zip.o" "$B/obj/logger_stub.o" "$B/obj/lsan_off.o" "${MZ_LIBS[@]}" -o "$SRC/$h"
"$CXX" "${FUZZ_CXXFLAGS[@]}" "$B/obj/$h.o" "$B/obj/standalone_main.o" "$B/obj/zip.o" "$B/obj/logger_stub.o" "$B/obj/lsan_off.o" "${MZ_LIBS[@]}" -o "$SRC/$h-standalone"
h=fuzz_lua_rpc
"$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" "${LH_INC[@]}" "${LUA_INC[@]}" -c "mayhem/harnesses/$h.cc" -o "$B/obj/$h.o"
"$CXX" "${FUZZ_CXXFLAGS[@]}" $LIB_FUZZING_ENGINE "$B/obj/$h.o" "${rpc_objs[@]}" "${objs[@]}" "$LUA_LIB" -lcrypto -ldl -pthread -o "$SRC/$h"
"$CXX" "${FUZZ_CXXFLAGS[@]}" "$B/obj/$h.o" "$B/obj/standalone_main.o" "${rpc_objs[@]}" "${objs[@]}" "$LUA_LIB" -lcrypto -ldl -pthread -o "$SRC/$h-standalone"
for h in fuzz_lua_json fuzz_lua_utils; do
  "$CXX" "${FUZZ_CXXFLAGS[@]}" "${INC[@]}" "${LUA_INC[@]}" -c "mayhem/harnesses/$h.cc" -o "$B/obj/$h.o"
  "$CXX" "${FUZZ_CXXFLAGS[@]}" $LIB_FUZZING_ENGINE "$B/obj/$h.o" "${lua_objs[@]}" "$B/obj/lsan_off.o" "$LUA_LIB" -ldl -o "$SRC/$h"
  "$CXX" "${FUZZ_CXXFLAGS[@]}" "$B/obj/$h.o" "$B/obj/standalone_main.o" "${lua_objs[@]}" "$B/obj/lsan_off.o" "$LUA_LIB" -ldl -o "$SRC/$h-standalone"
done

# 3) known-answer self-test, normal flags (no sanitizers), run by mayhem/test.sh
"$CXX" -std=c++23 -O2 $COVERAGE_FLAGS "${INC[@]}" mayhem/selftest/selftest.cc "${PROJECT_SRCS[@]}" \
  mayhem/harnesses/logger_stub.cc -lcrypto -o "$B/selftest/millennium_selftest"
# upstream's Lua plugin-API cases (tests/lua/cases), same module set as tests/lua/CMakeLists.txt
"$CC" -O2 -w $COVERAGE_FLAGS "${LUA_INC[@]}" -c src/lua_host/api/json.c -o "$B/selftest/json.o"
"$CXX" -std=c++23 -O2 $COVERAGE_FLAGS "${INC[@]}" "${LUA_INC[@]}" mayhem/selftest/lua_cases.cc \
  src/lua_host/api/regex.cc src/lua_host/api/datetime.cc src/lua_host/api/filesystem.cc src/lua_host/api/utils.cc \
  "$B/selftest/json.o" "$LUA_LIB" -ldl -o "$B/selftest/millennium_lua_cases"
