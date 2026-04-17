#!/usr/bin/env bash
set -euo pipefail

# Repeatedly snapshot /proc/<pid>/maps while cjpeg runs. Writes mapping result to trace_dir/snapshots/maps_*.txt and metadata to trace_dir/snapshot_index.csv.
# Usage:
#   ./collect_cjpeg_maps.sh [input.jpg] [trace_dir] [sample_interval_seconds] [quality]
# Example:
#   ./collect_cjpeg_maps.sh input.jpg trace_run_01 0.01 75

img="${1:-input.jpg}"
trace_dir="${2:-trace_$(date +%Y%m%d_%H%M%S)}"
sample_interval="${3:-0.01}"
quality="${4:-75}"

if ! command -v cjpeg >/dev/null 2>&1; then
    echo "Error: cjpeg is not installed or not in PATH." >&2
    exit 1
fi

if [ ! -f "$img" ]; then
    echo "Error: input file '$img' not found." >&2
    exit 1
fi

mkdir -p "$trace_dir/snapshots"
mkdir -p "$trace_dir/compressed"

output_img="$trace_dir/compressed/$(basename "${img%.*}.jpg")"
meta_csv="$trace_dir/snapshot_index.csv"

printf "snapshot,timestamp_unix_ns,elapsed_ms\n" > "$meta_csv"

start_ns=$(date +%s%N)

# Launch cjpeg in the background so we can sample while it executes.
cjpeg -quality "$quality" -optimize -outfile "$output_img" "$img" &
pid=$!

echo "Tracing PID: $pid"
echo "Input: $img"
echo "Output: $output_img"
echo "Trace directory: $trace_dir"

snapshot_num=0

while kill -0 "$pid" 2>/dev/null; do
    snapshot_num=$((snapshot_num + 1))
    ts_ns=$(date +%s%N)
    elapsed_ms=$(( (ts_ns - start_ns) / 1000000 ))

    printf -v snap_file "%s/snapshots/maps_%06d.txt" "$trace_dir" "$snapshot_num"

    if [ -r "/proc/$pid/maps" ]; then
        cp "/proc/$pid/maps" "$snap_file"
        printf "%d,%s,%d\n" "$snapshot_num" "$ts_ns" "$elapsed_ms" >> "$meta_csv"
    fi

    sleep "$sample_interval"
done

wait "$pid"
exit_code=$?

end_ns=$(date +%s%N)
elapsed_total_ms=$(( (end_ns - start_ns) / 1000000 ))

echo "cjpeg exit code: $exit_code"
echo "Snapshots captured: $snapshot_num"
echo "Total runtime (ms): $elapsed_total_ms"

echo "Run complete. Next step:"
echo "  ./analyze_map_lifetime.sh '$trace_dir'"
