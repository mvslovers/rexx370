#!/usr/bin/env bash
# scripts/host-profile.sh — WP-PERF-01 host profiling pipeline
#
# Builds the REXX/370 engine with -pg, runs the profiling driver, and
# produces gprof output under build/host-profile/.
#
# Usage:
#   ./scripts/host-profile.sh                  # full run, embedded microbench
#   ./scripts/host-profile.sh --source=PATH    # custom REXX source file
#   ./scripts/host-profile.sh --summary        # top-10 only, skip full profile.txt
#   ./scripts/host-profile.sh --clean          # remove build/host-profile/, exit
#
# Prerequisites (Linux):
#   gcc, gprof, and ../lstring370/ (parallel clone)
#
# Environment overrides:
#   CC=gcc-14     override the C compiler (default: gcc)
#   GPROF=gprof   override the gprof binary  (default: gprof)

set -euo pipefail

# ------------------------------------------------------------------ #
#  Configuration                                                       #
# ------------------------------------------------------------------ #

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/host-profile"
BINARY="${BUILD_DIR}/tstcps_host"
GMON="${BUILD_DIR}/gmon.out"

CC="${CC:-gcc}"
GPROF_CMD="${GPROF:-gprof}"

SOURCE_FLAG=""
SUMMARY_ONLY=0

# ------------------------------------------------------------------ #
#  Parse arguments                                                     #
# ------------------------------------------------------------------ #

for arg in "$@"; do
    case "${arg}" in
        --clean)
            echo "Removing ${BUILD_DIR} ..."
            rm -rf "${BUILD_DIR}"
            echo "Done."
            exit 0
            ;;
        --summary)
            SUMMARY_ONLY=1
            ;;
        --source=*)
            SOURCE_FLAG="${arg}"
            ;;
        *)
            echo "host-profile.sh: unknown option '${arg}'" >&2
            echo "usage: $0 [--source=PATH] [--summary] [--clean]" >&2
            exit 1
            ;;
    esac
done

# ------------------------------------------------------------------ #
#  Step 1: Preflight checks                                            #
# ------------------------------------------------------------------ #

echo "==> Preflight checks"

if ! command -v "${CC}" >/dev/null 2>&1; then
    echo "ERROR: C compiler not found: ${CC}" >&2
    echo "       Install gcc (e.g. 'sudo apt-get install gcc' on Debian/Ubuntu)." >&2
    echo "       Set CC= to override the compiler name." >&2
    exit 2
fi

if ! command -v "${GPROF_CMD}" >/dev/null 2>&1; then
    echo "ERROR: gprof not found: ${GPROF_CMD}" >&2
    echo "       Install binutils (e.g. 'sudo apt-get install binutils' on Debian/Ubuntu)." >&2
    echo "       Set GPROF= to override the gprof binary name." >&2
    exit 2
fi

LSTRING_DIR="${REPO_ROOT}/../lstring370"
if [ ! -d "${LSTRING_DIR}" ]; then
    echo "ERROR: lstring370 directory not found at ${LSTRING_DIR}" >&2
    echo "       Clone the lstring370 repository as a sibling of rexx370:" >&2
    echo "         git clone https://github.com/mvslovers/lstring370 ../lstring370" >&2
    exit 2
fi

echo "  CC      = $("${CC}" --version 2>&1 | head -1)"
echo "  gprof   = ${GPROF_CMD}"
echo "  lstring = ${LSTRING_DIR}"

# ------------------------------------------------------------------ #
#  Step 2: Build                                                       #
# ------------------------------------------------------------------ #

echo "==> Building tstcps_host (profiling build)"

mkdir -p "${BUILD_DIR}"

LSTRING_INC="-I ${LSTRING_DIR}/include"
LSTRING_SRC="${LSTRING_DIR}/src/lstr#cor.c \
             ${LSTRING_DIR}/src/lstr#cvt.c \
             ${LSTRING_DIR}/src/lstr#fmt.c \
             ${LSTRING_DIR}/src/lstr#srch.c \
             ${LSTRING_DIR}/src/lstr#sub.c \
             ${LSTRING_DIR}/src/lstr#wrd.c \
             ${LSTRING_DIR}/src/lstr#xlt.c"

ENGINE_SRC="${REPO_ROOT}/src/irx#init.c \
            ${REPO_ROOT}/src/irx#term.c \
            ${REPO_ROOT}/src/irx#stor.c \
            ${REPO_ROOT}/src/irx#anch.c \
            ${REPO_ROOT}/src/irx#env.c  \
            ${REPO_ROOT}/src/irx#uid.c  \
            ${REPO_ROOT}/src/irx#msid.c \
            ${REPO_ROOT}/src/irx#cond.c \
            ${REPO_ROOT}/src/irx#bif.c  \
            ${REPO_ROOT}/src/irx#bifs.c \
            ${REPO_ROOT}/src/irx#io.c   \
            ${REPO_ROOT}/src/irx#lstr.c \
            ${REPO_ROOT}/src/irx#tokn.c \
            ${REPO_ROOT}/src/irx#vpol.c \
            ${REPO_ROOT}/src/irx#pars.c \
            ${REPO_ROOT}/src/irx#ctrl.c \
            ${REPO_ROOT}/src/irx#exec.c \
            ${REPO_ROOT}/src/irx#arith.c"

# shellcheck disable=SC2086
"${CC}" \
    -I "${REPO_ROOT}/include" \
    -I "${REPO_ROOT}/contrib/lstring370-0.1.0-dev/include" \
    ${LSTRING_INC} \
    -Wall -Wextra -std=gnu99 -pg -O0 -g \
    -o "${BINARY}" \
    "${REPO_ROOT}/test/host/tstcps_host.c" \
    ${ENGINE_SRC} \
    ${LSTRING_SRC}

echo "  Binary: ${BINARY}"

# ------------------------------------------------------------------ #
#  Step 3: Run                                                         #
# ------------------------------------------------------------------ #

echo "==> Running driver"

cd "${BUILD_DIR}"

if [ -n "${SOURCE_FLAG}" ]; then
    # Translate --source=PATH to an absolute path before cd
    SOURCE_ARG="${SOURCE_FLAG#--source=}"
    if [[ "${SOURCE_ARG}" != /* ]]; then
        SOURCE_ARG="${OLDPWD}/${SOURCE_ARG}"
    fi
    RUN_ARGS="--source=${SOURCE_ARG}"
else
    RUN_ARGS=""
fi

time_start=$(date +%s%N)

# shellcheck disable=SC2086
"${BINARY}" ${RUN_ARGS} \
    > "${BUILD_DIR}/run.log" \
    2> "${BUILD_DIR}/timing.txt"

time_end=$(date +%s%N)
wall_ms=$(( (time_end - time_start) / 1000000 ))

echo "  Wall-clock: ${wall_ms} ms"
echo "  Timing (from driver stderr):"
sed 's/^/    /' "${BUILD_DIR}/timing.txt"
echo "  SAY output -> ${BUILD_DIR}/run.log"

if [ ! -f "${BUILD_DIR}/gmon.out" ]; then
    echo "ERROR: gmon.out not produced under ${BUILD_DIR}" >&2
    echo "       This is unexpected; check that the binary linked with -pg." >&2
    exit 3
fi

cd - >/dev/null

# ------------------------------------------------------------------ #
#  Step 4: Profile (full gprof; skipped with --summary)               #
# ------------------------------------------------------------------ #

if [ "${SUMMARY_ONLY}" -eq 0 ]; then
    echo "==> Running gprof (full profile)"
    "${GPROF_CMD}" "${BINARY}" "${GMON}" > "${BUILD_DIR}/profile.txt"
    echo "  Full profile -> ${BUILD_DIR}/profile.txt"
fi

# ------------------------------------------------------------------ #
#  Step 5: Extract top-10 hotspots                                     #
# ------------------------------------------------------------------ #

echo "==> Extracting top-10 hotspots"

# Parse the flat profile section from gprof output.
# Format of the flat profile (after the header lines):
#   %time  cumulative  self    calls  self  total  name
#   XX.XX   XX.XX      XX.XX  NNNN  ...   ...    func_name
#
# We locate the section by finding the "Flat profile:" header, skip
# the two separator lines and the column-header line, then take the
# next N non-blank data lines.

PROFILE_SRC="${BUILD_DIR}/profile.txt"
if [ "${SUMMARY_ONLY}" -eq 1 ]; then
    # For --summary we still need profile output to extract hotspots.
    PROFILE_SRC="${BUILD_DIR}/.tmp_profile.txt"
    "${GPROF_CMD}" "${BINARY}" "${GMON}" > "${PROFILE_SRC}"
fi

awk '
/^Flat profile:/ { in_flat=1; header_count=0; next }
in_flat && /^ *$/ { next }
in_flat && header_count < 3 { header_count++; next }
in_flat && header_count >= 3 {
    if (/^Call graph/) { exit }
    if (NF == 0) next
    rank++
    # fields: %time  cumulative  self  calls  self/call  total/call  name
    self_pct=$1; cum=$2; self_s=$3; calls=$4; fname=$NF
    printf "%d\t%s\t%s\t%s\t%s\t%s\n", rank, self_pct, cum, self_s, calls, fname
    if (rank >= 10) exit
}
' "${PROFILE_SRC}" > "${BUILD_DIR}/top-hotspots.txt"

# Clean up temp file if --summary
if [ "${SUMMARY_ONLY}" -eq 1 ]; then
    rm -f "${PROFILE_SRC}"
fi

echo "  Hotspots -> ${BUILD_DIR}/top-hotspots.txt"

# ------------------------------------------------------------------ #
#  Step 6: Summary line                                                #
# ------------------------------------------------------------------ #

TIMING_LINE=$(cat "${BUILD_DIR}/timing.txt" 2>/dev/null || echo "(no timing)")

# Top-3 hotspot functions for inline summary
TOP3=$(awk -F'\t' 'NR<=3 { printf "%s(%s%%) ", $6, $2 }' \
       "${BUILD_DIR}/top-hotspots.txt" 2>/dev/null || echo "(no hotspots)")

echo ""
echo "======================================================================"
echo "  Profile summary"
echo "  Wall-clock : ${wall_ms} ms"
echo "  Driver     : ${TIMING_LINE}"
echo "  Top-3      : ${TOP3}"
echo "  Output dir : ${BUILD_DIR}/"
echo "======================================================================"
