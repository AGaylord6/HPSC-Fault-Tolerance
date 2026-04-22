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

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <input.ppm> <output.jpg> [QUALITY] [TARGET_MAPPING] [--DRY_RUN 0|1] [--FLIPS_PER_PFN N] [--PFN_LIST path] [--CJPEG_BIN path]"
    echo "  TARGET_MAPPING examples: heap, all, libc, /usr/lib"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT="$1"
OUTPUT="$2"
shift 2

QUALITY=75
TARGET_MAPPING=heap
DRY_RUN=1
FLIPS_PER_PFN=1
PFN_LIST="${SCRIPT_DIR}/pfns.txt"
CJPEG_BIN="cjpeg"

if [[ $# -gt 0 && ${1:0:1} != '-' ]]; then
    QUALITY="$1"
    shift
fi

if [[ $# -gt 0 && ${1:0:1} != '-' ]]; then
    TARGET_MAPPING="$1"
    shift
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --DRY_RUN)
            DRY_RUN="${2:?missing value for --DRY_RUN}"
            shift 2
            ;;
        --FLIPS_PER_PFN)
            FLIPS_PER_PFN="${2:?missing value for --FLIPS_PER_PFN}"
            shift 2
            ;;
        --PFN_LIST)
            PFN_LIST="${2:?missing value for --PFN_LIST}"
            shift 2
            ;;
        --CJPEG_BIN)
            CJPEG_BIN="${2:?missing value for --CJPEG_BIN}"
            shift 2
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

FIND_PFNS_BIN="${SCRIPT_DIR}/find_pfns"
INJECT_BIN="${SCRIPT_DIR}/inject_pfn_faults"
TEMP_PFN_LIST="${PFN_LIST}.tmp"
DATASET_DIR="${SCRIPT_DIR}/../Dataset"
CACHE_DIR="${DATASET_DIR}/cache"
INPUT_BASENAME="$(basename "${INPUT}")"
INPUT_STEM="${INPUT_BASENAME%.*}"
CACHE_OUTPUT="${CACHE_DIR}/${INPUT_STEM}_q${QUALITY}.jpg"

ensure_cached_output() {
    mkdir -p "${CACHE_DIR}"

    if [[ ! -s "${CACHE_OUTPUT}" ]]; then
        local cache_tmp
        local cache_stripped_tmp
        cache_tmp="$(mktemp "${CACHE_OUTPUT}.tmp.XXXXXX")"
        cache_stripped_tmp="$(mktemp "${CACHE_OUTPUT}.stripped.XXXXXX")"
        "${CJPEG_BIN}" -quality "${QUALITY}" "${INPUT}" > "${cache_tmp}"
        jpegtran -copy none "${cache_tmp}" > "${cache_stripped_tmp}"
        mv -f "${cache_stripped_tmp}" "${CACHE_OUTPUT}"
        rm -f "${cache_tmp}"
	    echo "Created cached (and stripped) baseline at ${CACHE_OUTPUT}"
    else
	    echo "Using cached (and stripped) baseline at ${CACHE_OUTPUT}"
    fi
}

verify_output() {
    local output_stripped_tmp
    output_stripped_tmp="$(mktemp "${OUTPUT}.stripped.XXXXXX")"
    jpegtran -copy none "${OUTPUT}" > "${output_stripped_tmp}"
    mv -f "${output_stripped_tmp}" "${OUTPUT}"
    echo "Stripped metadata from ${OUTPUT} for compare"
    if cmp -s "${OUTPUT}" "${CACHE_OUTPUT}"; then
        echo "Output matches cached baseline"
        return 0
    fi

    echo "Output differs from cached baseline"
    return 6
}

if ! command -v "${CJPEG_BIN}" >/dev/null 2>&1; then
    echo "cjpeg not found in PATH (or CJPEG_BIN is invalid): ${CJPEG_BIN}"
    exit 2
fi

if [[ ! -f "${INPUT}" ]]; then
    echo "Input file does not exist: ${INPUT}"
    exit 3
fi

mkdir -p "$(dirname "${OUTPUT}")"
ensure_cached_output

echo "Using QUALITY=${QUALITY} TARGET_MAPPING=${TARGET_MAPPING} DRY_RUN=${DRY_RUN} FLIPS_PER_PFN=${FLIPS_PER_PFN} PFN_LIST=${PFN_LIST}"

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
verify_status=0
if ! verify_output; then
    verify_status=6
fi

if [[ ${status} -ne 0 ]]; then
    exit "${status}"
fi

exit "${verify_status}"
