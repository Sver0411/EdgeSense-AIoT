#!/usr/bin/env bash
#
# test_host.sh — build and run the portable-C host test suite.
#
# Compiler discipline (Phase 1A requirement):
#   * EdgeSense's own sources are built with -Wall -Wextra -Werror. A warning is
#     a failure.
#   * Vendored upstream sources are built with -Wall -Wextra but NOT -Werror.
#     Warnings from code we did not write and must not edit are reported, not
#     turned into our build failure. The separation is deliberate: silently
#     silencing vendor warnings with -Wno-* would hide a real incompatibility.
#
# Exit status: 0 when every suite passes, 1 otherwise.
#
# Usage:
#   scripts/test_host.sh            # build and run everything
#   CC=clang scripts/test_host.sh   # use a different C compiler

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"
BUILD="$ROOT/build/host_tests"

EDGE_FLAGS=(-std=c11 -Wall -Wextra -Werror -O1 -g)
VENDOR_FLAGS=(-std=c11 -Wall -Wextra -O1 -g)

SENSOR_DIR="$ROOT/firmware/common/sensor"
CONFIG_DIR="$ROOT/firmware/common/config"
PROTOCOL_DIR="$ROOT/firmware/common/protocol"
VENDOR_DIR="$ROOT/firmware/common/vendor/adaptivesense"
VENDOR_EG_DIR="$ROOT/firmware/common/vendor/eventguard"
TEST_DIR="$ROOT/tests/host"

INCLUDES=(-I"$SENSOR_DIR" -I"$CONFIG_DIR" -I"$PROTOCOL_DIR" -I"$VENDOR_DIR" -I"$VENDOR_EG_DIR" -I"$TEST_DIR")

EDGE_SRC=(
    "$SENSOR_DIR/edge_channel.c"
    "$SENSOR_DIR/edge_sensor_state.c"
    "$CONFIG_DIR/edge_nodes.c"
    "$PROTOCOL_DIR/edge_protocol.c"
)
VENDOR_SRC=(
    "$VENDOR_DIR/sensor_supervisor.c"
    "$VENDOR_EG_DIR/protocol.c"
)

mkdir -p "$BUILD" || exit 1

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "FAIL compile: C compiler '$CC' not found on PATH"
    exit 1
fi

echo "host test suite  (CC=$CC)"
echo "  edge flags   : ${EDGE_FLAGS[*]}"
echo "  vendor flags : ${VENDOR_FLAGS[*]}"
echo

# Compile EdgeSense's own sources once with -Werror. If any of them warn, that is
# a build failure before a single test runs.
edge_objs=()
for src in "${EDGE_SRC[@]}"; do
    obj="$BUILD/$(basename "${src%.c}").o"
    if ! "$CC" "${EDGE_FLAGS[@]}" "${INCLUDES[@]}" -c "$src" -o "$obj" 2>"$BUILD/cc.log"; then
        echo "FAIL compile (edge, -Werror): $(basename "$src")"
        sed 's/^/      /' "$BUILD/cc.log"
        exit 1
    fi
    if [ -s "$BUILD/cc.log" ]; then
        echo "FAIL compile: warnings treated as errors in $(basename "$src")"
        sed 's/^/      /' "$BUILD/cc.log"
        exit 1
    fi
    edge_objs+=("$obj")
done
echo "PASS compile edge sources (-Wall -Wextra -Werror clean): ${#edge_objs[@]} file(s)"

# Compile vendored sources without -Werror, but surface any warning.
vendor_objs=()
vendor_warnings=0
for src in "${VENDOR_SRC[@]}"; do
    obj="$BUILD/vendor_$(basename "${src%.c}").o"
    if ! "$CC" "${VENDOR_FLAGS[@]}" "${INCLUDES[@]}" -c "$src" -o "$obj" 2>"$BUILD/cc_vendor.log"; then
        echo "FAIL compile (vendor): $(basename "$src")"
        sed 's/^/      /' "$BUILD/cc_vendor.log"
        exit 1
    fi
    if [ -s "$BUILD/cc_vendor.log" ]; then
        vendor_warnings=$((vendor_warnings + 1))
        echo "WARN vendor compiler diagnostics in $(basename "$src") (not fatal, never edited):"
        sed 's/^/      /' "$BUILD/cc_vendor.log"
    fi
    vendor_objs+=("$obj")
done
if [ "$vendor_warnings" -eq 0 ]; then
    echo "PASS compile vendored sources (-Wall -Wextra clean): ${#vendor_objs[@]} file(s)"
else
    echo "INFO vendored sources compiled with $vendor_warnings file(s) emitting diagnostics"
fi
echo

# Every suite is (name, test source). All EdgeSense and vendored objects are
# linked into each test binary: the units are small, and linking everything keeps
# this script free of per-suite bookkeeping that would quietly rot.
suites=(
    "edge_channel|$TEST_DIR/test_edge_channel.c"
    "edge_sensor_state|$TEST_DIR/test_edge_sensor_state.c"
    "vendor_supervisor_contract|$TEST_DIR/test_vendor_supervisor_contract.c"
    "node_config|$TEST_DIR/test_node_config.c"
    "edge_protocol|$TEST_DIR/test_edge_protocol.c"
)

passed=0
failed=0
failed_names=()

for entry in "${suites[@]}"; do
    IFS='|' read -r name test_src <<<"$entry"

    link_objs=("${edge_objs[@]}" "${vendor_objs[@]}")

    bin="$BUILD/$name"
    if ! "$CC" -std=c11 -Wall -Wextra -Werror "${INCLUDES[@]}" "$test_src" \
        "${link_objs[@]}" -lm -o "$bin" 2>"$BUILD/link.log"; then
        echo "FAIL link: $name"
        sed 's/^/      /' "$BUILD/link.log"
        failed=$((failed + 1))
        failed_names+=("$name")
        continue
    fi

    if "$bin" >"$BUILD/$name.out" 2>&1; then
        sed 's/^/    /' "$BUILD/$name.out"
        echo "PASS $name"
        passed=$((passed + 1))
    else
        sed 's/^/    /' "$BUILD/$name.out"
        echo "FAIL $name"
        failed=$((failed + 1))
        failed_names+=("$name")
    fi
    echo
done

echo "---------------------------------------------"
echo "host tests: $passed passed, $failed failed"
if [ "$failed" -ne 0 ]; then
    echo "failed suites: ${failed_names[*]}"
    exit 1
fi
echo "SKIP hardware test suites: no on-device execution in this script (see docs/hardware/wiring.md)"
exit 0
