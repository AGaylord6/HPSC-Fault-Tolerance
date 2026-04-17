#!/usr/bin/bash
set -euo pipefail

# Barebones flow:
# 1) Start cjpeg in background.
# 2) Pause it.
# 3) Resolve PFNs for selected mappings.
# Call C program that takes in PID and target mapping (heap, file/library, address range, etc)
#   Read virtual memory maps
#   Find physical pages backing the target mappings
# 4) Inject faults into those PFNs.
# Call C program that injects Single-event upset (SEU) into given PFN at given frequency (or address within it)
# 5) Resume and wait for completion.
# Resume cjpeg process and observe for crashes, hangs, or output corruption

if [[ $# -lt 2 || $# -gt 4 ]]; then
    echo "Usage: $0 <input.ppm> <output.jpg> [quality] [target_mapping]"
    echo "  target_mapping examples: heap, all, libc, /usr/lib"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT="$1"
OUTPUT="$2"
QUALITY="${3:-75}"
TARGET_MAPPING="${4:-heap}"

PFN_LIST="${PFN_LIST:-${SCRIPT_DIR}/pfns.txt}"
FLIPS_PER_PFN="${FLIPS_PER_PFN:-1}"
DRY_RUN="${DRY_RUN:-1}"
CJPEG_BIN="${CJPEG_BIN:-cjpeg}"

FIND_PFNS_BIN="${SCRIPT_DIR}/find_pfns"
INJECT_BIN="${SCRIPT_DIR}/inject_pfn_faults"
TEMP_PFN_LIST="${PFN_LIST}.tmp"

if ! command -v "${CJPEG_BIN}" >/dev/null 2>&1; then
    echo "cjpeg not found in PATH (or CJPEG_BIN is invalid): ${CJPEG_BIN}"
    exit 2
fi

if [[ ! -f "${INPUT}" ]]; then
    echo "Input file does not exist: ${INPUT}"
    exit 3
fi

mkdir -p "$(dirname "${OUTPUT}")"

cc -O2 -std=c11 -Wall -Wextra -o "${FIND_PFNS_BIN}" "${SCRIPT_DIR}/find_pfns.c"
cc -O2 -std=c11 -Wall -Wextra -o "${INJECT_BIN}" "${SCRIPT_DIR}/inject_pfn_faults.c"

"${CJPEG_BIN}" -quality "${QUALITY}" "${INPUT}" > "${OUTPUT}" &
pid=$!

cleanup() {
    if kill -0 "${pid}" 2>/dev/null; then
        kill -CONT "${pid}" 2>/dev/null || true
    fi
    rm -f "${TEMP_PFN_LIST}"
}
trap cleanup EXIT

if ! kill -0 "${pid}" 2>/dev/null; then
    echo "Failed to start cjpeg process"
    exit 4
fi

echo "Started cjpeg with PID ${pid}"

sleep 0.1

# Hard pause to avoid shell-job-control quirks with SIGTSTP (-TSTP)
kill -STOP "${pid}"
echo "Paused PID ${pid}"

"${FIND_PFNS_BIN}" "${pid}" "${TARGET_MAPPING}" "${TEMP_PFN_LIST}"

if [[ ! -s "${TEMP_PFN_LIST}" ]]; then
    echo "No PFNs found for target '${TARGET_MAPPING}'."
    echo "If pages are present but PFN is zero, run with CAP_SYS_ADMIN/root and relaxed pagemap policy."
    exit 5
fi

sort -u "${TEMP_PFN_LIST}" > "${PFN_LIST}"
echo "Wrote deduplicated PFN list to ${PFN_LIST}"

if [[ "${DRY_RUN}" == "1" ]]; then
    "${INJECT_BIN}" "${PFN_LIST}" "${FLIPS_PER_PFN}" --dry-run
else
    "${INJECT_BIN}" "${PFN_LIST}" "${FLIPS_PER_PFN}"
fi

# Resume execution.
kill -CONT "${pid}"
echo "Resumed PID ${pid}"

# Observe whether cjpeg exits normally or crashes.
set +e
wait "${pid}"
status=$?
set -e

echo "cjpeg exit code: ${status}"
exit "${status}"
