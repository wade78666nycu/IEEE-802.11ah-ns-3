#!/usr/bin/env bash
# Grid spatial-reuse comparison: sweep pair_spacing x data_rate x {full power, GradPC},
# extract the same metrics as run_comparison.sh (PDR / throughput / avg delay / RERR /
# energy) from each run_summary.txt, and print an aligned table.
#
# Usage:
#   ./run_grid_comparison.sh [--spacings "300 400 600 800"] [--rates "500Kbps 1Mbps 2Mbps"] \
#                            [--pairs_per_row 3] [--link_distance 60] [--optimized] [--dry-run]

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="$ROOT_DIR/output_file/.grid_results"

SPACINGS="300 400 600 800"
RATES="500Kbps 1Mbps 2Mbps"
PAIRS_PER_ROW=3
LINK_DISTANCE=60
USE_OPTIMIZED=false
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --spacings)      SPACINGS="$2";      shift 2 ;;
        --rates)         RATES="$2";         shift 2 ;;
        --pairs_per_row) PAIRS_PER_ROW="$2"; shift 2 ;;
        --link_distance) LINK_DISTANCE="$2"; shift 2 ;;
        --optimized)     USE_OPTIMIZED=true; shift ;;
        --dry-run)       DRY_RUN=true;       shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

REPORT="$ROOT_DIR/output_file/grid_comparison_report.txt"

if $USE_OPTIMIZED; then
    WAF_BUILD="./waf --build-profile=optimized build"
    BIN="$ROOT_DIR/build/optimized/scratch/grid-exp/grid-exp"
    LIB_DIR="$ROOT_DIR/build/optimized"
else
    WAF_BUILD="./waf build"
    BIN="$ROOT_DIR/build/scratch/grid-exp/grid-exp"
    LIB_DIR="$ROOT_DIR/build"
fi

# Two modes compared at the SAME default power (15 dBm) for a fair comparison:
#   full   = power control off (everyone fixed at 15 dBm)
#   gradpc = power control on, unlocked (ch0 also reduced, extra_tx_distance=0)
mode_args() {
    case "$1" in
        full)   echo "--enable_power_control=false --tx_power=15" ;;
        gradpc) echo "--enable_power_control=true  --tx_power=15 --reduce_default=true --extra_tx_distance=0" ;;
    esac
}

run_case_bg() {
    local spacing="$1" rate="$2" mode="$3"
    local sname="grid_s${spacing}_r${rate}_${mode}"
    local result_file="$RESULTS_DIR/${spacing}_${rate}_${mode}"

    local common="--pairs_per_row=$PAIRS_PER_ROW --pair_spacing=$spacing --link_distance=$LINK_DISTANCE"
    common+=" --data_rate=$rate --show_log=false --export_node_info=false"

    echo "  → [spacing=$spacing rate=$rate mode=$mode]"
    if $DRY_RUN; then
        echo "    CMD: $BIN --scenario_name=$sname $common $(mode_args "$mode")"
        return
    fi

    (
        tmpdir=$(mktemp -d)
        cd "$tmpdir"
        LD_LIBRARY_PATH="$LIB_DIR" "$BIN" --scenario_name="$sname" $common $(mode_args "$mode") > /dev/null 2>&1
        local summary="$tmpdir/output_file/${sname}/${sname}_run_summary.txt"
        grep -E "^(pdr_percent|throughput_bps|avg_delay_ms|total_rerr_sent|total_energy_j)=" \
            "$summary" 2>/dev/null > "$result_file" || true
        rm -rf "$tmpdir" 2>/dev/null || true
        echo "  ✓ [spacing=$spacing rate=$rate mode=$mode] done"
    ) &
}

extract() { grep "^${2}=" "${1}" 2>/dev/null | cut -d= -f2 || echo "N/A"; }
fmt() {
    local val="$1" f="$2"
    [[ "$val" == "N/A" || -z "$val" ]] && echo "N/A" || awk "BEGIN{printf \"$f\", $val}"
}

print_summary() {
    local header sep body
    header=$(printf "%-9s %-9s %-8s %8s %11s %11s %7s %10s\n" \
        "Spacing" "Rate" "Mode" "PDR(%)" "Thru(Kbps)" "AvgDly(ms)" "RERR" "Energy(J)")
    sep="$(printf '%.0s─' {1..78})"
    body=""

    for spacing in $SPACINGS; do
        for rate in $RATES; do
            for mode in full gradpc; do
                local f="$RESULTS_DIR/${spacing}_${rate}_${mode}"
                local pdr thru delay rerr energy
                pdr=$(fmt   "$(extract "$f" "pdr_percent")"      "%.2f")
                thru=$(fmt  "$(extract "$f" "throughput_bps")"   "%.2f")
                delay=$(fmt "$(extract "$f" "avg_delay_ms")"     "%.2f")
                rerr=$(extract "$f" "total_rerr_sent")
                energy=$(fmt "$(extract "$f" "total_energy_j")"  "%.4f")
                [[ "$thru" != "N/A" ]] && thru=$(awk "BEGIN{printf \"%.2f\", $thru/1000}")
                body+=$(printf "%-9s %-9s %-8s %8s %11s %11s %7s %10s\n" \
                    "$spacing" "$rate" "$mode" "$pdr" "$thru" "$delay" "$rerr" "$energy")
                body+=$'\n'
            done
            body+=$'\n'   # blank line between rate groups for readability
        done
    done

    local report
    report="$(printf '%s\n%s\n%s\n' "$header" "$sep" "$body")"
    echo ""
    echo "$report"
    mkdir -p "$(dirname "$REPORT")"
    echo "$report" > "$REPORT"
    echo "  Report saved: $REPORT"
    "$ROOT_DIR/.venv/bin/python3" "$ROOT_DIR/grid_report_to_xlsx.py" "$REPORT" 2>/dev/null \
        && echo "  Excel saved:  ${REPORT%.txt}.xlsx" \
        || echo "  (xlsx skipped: .venv/bin/python3 or openpyxl missing)"
}

echo "════════════════════════════════════════════════════════════"
echo "  Building grid-exp..."
echo "════════════════════════════════════════════════════════════"
(cd "$ROOT_DIR" && $WAF_BUILD 2>&1) | tail -3

mkdir -p "$RESULTS_DIR"
echo "════════════════════════════════════════════════════════════"
echo "  Launching grid sweep: spacings=[$SPACINGS] rates=[$RATES]"
echo "════════════════════════════════════════════════════════════"

for spacing in $SPACINGS; do
    for rate in $RATES; do
        for mode in full gradpc; do
            run_case_bg "$spacing" "$rate" "$mode"
        done
    done
done

if ! $DRY_RUN; then
    echo ""
    echo "  Waiting for all cases to finish..."
    wait
    echo ""
    echo "  All done."
    print_summary
    rm -rf "$RESULTS_DIR"
fi
