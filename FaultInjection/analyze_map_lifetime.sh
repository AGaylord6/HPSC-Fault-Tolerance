#!/usr/bin/env bash
set -euo pipefail

# Analyze a snapshot directory produced by collect_cjpeg_maps.sh.
# Usage:
#   ./analyze_map_lifetime.sh <trace_dir>

trace_dir="${1:-}"
if [ -z "$trace_dir" ]; then
    echo "Usage: $0 <trace_dir>" >&2
    exit 1
fi

snapshot_dir="$trace_dir/snapshots"
analysis_dir="$trace_dir/analysis"

if [ ! -d "$snapshot_dir" ]; then
    echo "Error: snapshot directory '$snapshot_dir' does not exist." >&2
    exit 1
fi

mkdir -p "$analysis_dir"

shopt -s nullglob
snapshot_files=("$snapshot_dir"/maps_*.txt)
shopt -u nullglob

snapshot_count=${#snapshot_files[@]}
if [ "$snapshot_count" -eq 0 ]; then
    echo "Error: no snapshot files found in '$snapshot_dir'." >&2
    exit 1
fi

presence_tsv="$analysis_dir/mapping_presence.tsv"
always_txt="$analysis_dir/always_present_mappings.txt"
transient_tsv="$analysis_dir/transient_mappings.tsv"
appear_after_start_txt="$analysis_dir/appeared_after_start.txt"
disappear_before_end_txt="$analysis_dir/disappeared_before_end.txt"

# Build per-mapping statistics:
# count = number of snapshots in which this exact mapping line exists
# first = first snapshot index where seen
# last  = last snapshot index where seen
awk '
BEGIN {
    OFS = "\t"
}
FNR == 1 {
    file_index++
    delete seen_in_file
}
{
    line = $0
    if (!(line in seen_in_file)) {
        seen_in_file[line] = 1
        count[line]++
        if (!(line in first)) {
            first[line] = file_index
        }
        last[line] = file_index
    }
}
END {
    for (line in count) {
        print count[line], first[line], last[line], line
    }
}
' "${snapshot_files[@]}" | sort -t $'\t' -k1,1nr -k2,2n > "$presence_tsv"

awk -F '\t' -v n="$snapshot_count" '$1 == n { print $4 }' "$presence_tsv" > "$always_txt"
awk -F '\t' -v n="$snapshot_count" '$1 < n { print }' "$presence_tsv" > "$transient_tsv"
awk -F '\t' '$2 > 1 { print $4 }' "$presence_tsv" > "$appear_after_start_txt"
awk -F '\t' -v n="$snapshot_count" '$3 < n { print $4 }' "$presence_tsv" > "$disappear_before_end_txt"

unique_count=$(wc -l < "$presence_tsv")
always_count=$(wc -l < "$always_txt")
transient_count=$(wc -l < "$transient_tsv")
appear_count=$(wc -l < "$appear_after_start_txt")
disappear_count=$(wc -l < "$disappear_before_end_txt")

summary_txt="$analysis_dir/summary.txt"
{
    echo "Trace directory: $trace_dir"
    echo "Snapshots analyzed: $snapshot_count"
    echo "Unique mapping lines (exact): $unique_count"
    echo "Mappings present in every snapshot: $always_count"
    echo "Transient mappings (appear/disappear at least once): $transient_count"
    echo "Mappings first seen after snapshot 1: $appear_count"
    echo "Mappings missing before final snapshot: $disappear_count"
    echo
    echo "Generated files:"
    echo "  $presence_tsv"
    echo "  $always_txt"
    echo "  $transient_tsv"
    echo "  $appear_after_start_txt"
    echo "  $disappear_before_end_txt"
} > "$summary_txt"

cat "$summary_txt"

echo
echo "Columns in $presence_tsv and $transient_tsv:"
echo "  1: snapshots_present"
echo "  2: first_snapshot_index"
echo "  3: last_snapshot_index"
echo "  4: full /proc/<pid>/maps line"
