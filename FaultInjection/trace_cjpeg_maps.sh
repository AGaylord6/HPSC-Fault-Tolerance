#!/usr/bin/env bash
set -euo pipefail

# End-to-end helper: collect snapshots during cjpeg runtime, then analyze them.
# Usage:
#   ./trace_cjpeg_maps.sh [input.jpg] [trace_dir] [sample_interval_seconds] [quality]

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

img="${1:-input.jpg}"
trace_dir="${2:-trace_$(date +%Y%m%d_%H%M%S)}"
sample_interval="${3:-0.01}"
quality="${4:-75}"

"$script_dir/collect_cjpeg_maps.sh" "$img" "$trace_dir" "$sample_interval" "$quality"
"$script_dir/analyze_map_lifetime.sh" "$trace_dir"

echo
echo "Done. Inspect: $trace_dir/analysis/summary.txt"
