#!/usr/bin/env bash
# mayhem/test.sh — runs the known-answer self-test built by mayhem/build.sh. Each check asserts
# a decoded value (plugin id/name/version from a .star package, its JS bundles and asset index,
# star_decompress output, semver ordering, the rewritten command line, theme colours).
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# 13 known-answer checks (mayhem/selftest/selftest.cc) + upstream's 5 Lua API case files
EXPECTED=$(( 13 + $(ls "$SRC"/tests/lua/cases/*.lua | wc -l) ))
bin="$SRC/mayhem/build/selftest/millennium_selftest"
lua_bin="$SRC/mayhem/build/selftest/millennium_lua_cases"
if [ ! -x "$bin" ] || [ ! -x "$lua_bin" ]; then
  echo "missing $bin or $lua_bin — run mayhem/build.sh first" >&2
  emit_ctrf millennium-selftest 0 "$EXPECTED"
  exit 1
fi

out="$("$bin" "$SRC/mayhem/selftest" 2>&1)"; rc=$?
out2="$("$lua_bin" "$SRC/tests/lua/cases" 2>&1)"; rc2=$?
out="$out"$'\n'"$out2"; [ "$rc2" -ne 0 ] && rc=$rc2
echo "$out"
passed=$(grep -c '^PASS ' <<<"$out" || true)
failed=$(grep -c '^FAIL ' <<<"$out" || true)
# a check that never reported (crash, early exit) counts as failed
missing=$(( EXPECTED - passed - failed ))
[ "$missing" -gt 0 ] && failed=$(( failed + missing ))
[ "$rc" -ne 0 ] && [ "$failed" -eq 0 ] && failed=1
emit_ctrf millennium-selftest "$passed" "$failed"
