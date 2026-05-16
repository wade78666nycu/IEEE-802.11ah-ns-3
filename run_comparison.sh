#!/usr/bin/env bash
# Comparison script: 6 configurations × 2 traffic levels × N seeds, all cases run in PARALLEL.
# Simulations run in per-case temp dirs and are deleted afterwards — no cmp_* dirs left behind.
#
# Usage:
#   ./run_comparison.sh [--seeds "4 5 7 9 10"] [--num_nodes 100] [--optimized] [--dry-run]
#   --seeds "..."     space-separated list of seeds (default: "9", valid: 4 5 7 9 10)
#   --num_nodes N     number of nodes (default: 100, valid: 30 50 100)
#   --optimized       use optimized build instead of debug
#   --dry-run         print commands without running

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="$ROOT_DIR/output_file/.cmp_results"

SEEDS="9"
NUM_NODES=100
USE_OPTIMIZED=false
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --seeds)     SEEDS="$2";     shift 2 ;;
        --seed)      SEEDS="$2";     shift 2 ;;
        --num_nodes) NUM_NODES="$2"; shift 2 ;;
        --optimized) USE_OPTIMIZED=true; shift ;;
        --dry-run)   DRY_RUN=true;   shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

REPORT="$ROOT_DIR/output_file/comparison_report_n${NUM_NODES}.txt"

if $USE_OPTIMIZED; then
    WAF_BUILD="./waf --build-profile=optimized build"
    BIN="$ROOT_DIR/build/optimized/scratch/rand-exp/rand-exp"
    LIB_DIR="$ROOT_DIR/build/optimized"
else
    WAF_BUILD="./waf build"
    BIN="$ROOT_DIR/build/scratch/rand-exp/rand-exp"
    LIB_DIR="$ROOT_DIR/build"
fi

# ── run one case in background ──────────────────────────────────────────────

run_case_bg() {
    local seed="$1" traffic="$2" case_key="$3"
    shift 3

    local sname="cmp_s${seed}_${traffic}_${case_key}"
    local result_file="$RESULTS_DIR/${seed}_${traffic}_${case_key}"

    echo "  → [seed$seed/$traffic/$case_key]"

    if $DRY_RUN; then
        echo "    CMD: $BIN --scenario_name=$sname $*"
        return
    fi

    (
        tmpdir=$(mktemp -d)
        cd "$tmpdir"
        LD_LIBRARY_PATH="$LIB_DIR" "$BIN" --scenario_name="$sname" "$@" > /dev/null 2>&1

        summary="$tmpdir/output_file/${sname}/${sname}_run_summary.txt"
        grep -E "^(pdr_percent|throughput_bps|avg_delay_ms|total_rerr_sent|total_energy_j|energy_per_byte_j)=" \
            "$summary" 2>/dev/null > "$result_file" || true

        rm -rf "$tmpdir" 2>/dev/null || true
        echo "  ✓ [seed$seed/$traffic/$case_key] done"
    ) &
}

# ── summary after all runs ───────────────────────────────────────────────────

extract() { grep "^${2}=" "${1}" 2>/dev/null | cut -d= -f2 || echo "N/A"; }
fmt() {
    local val="$1" fmt="$2"
    [[ "$val" == "N/A" ]] && echo "N/A" || awk "BEGIN{printf \"$fmt\", $val}"
}

print_summary() {
    local seeds_list="$1"
    local header sep body

    header=$(printf "%-6s %-12s %-28s %8s %10s %10s %8s %10s %12s\n" \
        "Seed" "Traffic" "Case" "PDR(%)" "Thru(Kbps)" "AvgDly(ms)" "RERR" "Energy(J)" "E/Byte(mJ)")
    sep="$(printf '%.0s─' {1..104})"
    body=""

    for seed in $seeds_list; do
        for traffic in light heavy very_heavy; do
            for case_key in 01_std_aodv 06_gradpc_no_chswitch 02_full_power 03_low_power_fixed 04_gradpc 05_gradpc_prefer_low; do
                local f="$RESULTS_DIR/${seed}_${traffic}_${case_key}"

                local pdr thru delay rerr energy epb
                pdr=$(fmt    "$(extract "$f" "pdr_percent")"       "%.2f")
                thru=$(fmt   "$(extract "$f" "throughput_bps")"    "%.2f")
                delay=$(fmt  "$(extract "$f" "avg_delay_ms")"      "%.2f")
                rerr=$(extract "$f" "total_rerr_sent")
                energy=$(fmt "$(extract "$f" "total_energy_j")"    "%.4f")
                epb=$(fmt    "$(extract "$f" "energy_per_byte_j")" "%.6f")

                # convert bps → Kbps
                [[ "$thru" != "N/A" ]] && thru=$(awk "BEGIN{printf \"%.2f\", $thru/1000}")
                # convert J/byte → mJ/byte
                [[ "$epb"  != "N/A" ]] && epb=$(awk "BEGIN{printf \"%.4f\", $epb*1000}")

                body+=$(printf "%-6s %-12s %-28s %8s %10s %10s %8s %10s %12s\n" \
                    "$seed" "$traffic" "$case_key" "$pdr" "$thru" "$delay" "$rerr" "$energy" "$epb")
                body+=$'\n'
            done
        done
    done

    local report
    report="$(printf '%s\n%s\n%s\n' "$header" "$sep" "$body")"

    echo ""
    echo "$report"
    mkdir -p "$(dirname "$REPORT")"
    echo "$report" > "$REPORT"
    echo "  Report saved: $REPORT"
    "$ROOT_DIR/.venv/bin/python3" "$ROOT_DIR/report_to_xlsx.py" "$REPORT" 2>/dev/null || true
}

# ── launch all cases for one seed ────────────────────────────────────────────

launch_seed() {
    local seed="$1"

    local cl="--seed=$seed --num_nodes=$NUM_NODES --num_flows=7  --data_rate=30Kbps --send_packet_num=500 --show_log=false --export_node_info=false"
    local ch="--seed=$seed --num_nodes=$NUM_NODES --num_flows=14 --data_rate=30Kbps --send_packet_num=500 --show_log=false --export_node_info=false"
    local cv="--seed=$seed --num_nodes=$NUM_NODES --num_flows=20 --data_rate=30Kbps --send_packet_num=500 --show_log=false --export_node_info=false"

    run_case_bg $seed light "01_std_aodv"          $cl --enable_hello=false --device_num=1
    run_case_bg $seed light "02_full_power"         $cl --enable_hello=true --enable_power_control=false --tx_power=15
    run_case_bg $seed light "03_low_power_fixed"    $cl --enable_hello=true --enable_power_control=false --tx_power=8
    run_case_bg $seed light "04_gradpc"             $cl --enable_hello=true --enable_power_control=true
    run_case_bg $seed light "05_gradpc_prefer_low"  $cl --enable_hello=true --enable_power_control=true --prefer_low_power_channel=true
    run_case_bg $seed light "06_gradpc_no_chswitch" $cl --enable_hello=true --enable_power_control=true --enable_channel_switch_on_retry=false

    run_case_bg $seed heavy "01_std_aodv"          $ch --enable_hello=false --device_num=1
    run_case_bg $seed heavy "02_full_power"         $ch --enable_hello=true --enable_power_control=false --tx_power=15
    run_case_bg $seed heavy "03_low_power_fixed"    $ch --enable_hello=true --enable_power_control=false --tx_power=8
    run_case_bg $seed heavy "04_gradpc"             $ch --enable_hello=true --enable_power_control=true
    run_case_bg $seed heavy "05_gradpc_prefer_low"  $ch --enable_hello=true --enable_power_control=true --prefer_low_power_channel=true
    run_case_bg $seed heavy "06_gradpc_no_chswitch" $ch --enable_hello=true --enable_power_control=true --enable_channel_switch_on_retry=false

    if [[ "$NUM_NODES" -ge 100 ]]; then
        run_case_bg $seed very_heavy "01_std_aodv"          $cv --enable_hello=false --device_num=1
        run_case_bg $seed very_heavy "02_full_power"         $cv --enable_hello=true --enable_power_control=false --tx_power=15
        run_case_bg $seed very_heavy "03_low_power_fixed"    $cv --enable_hello=true --enable_power_control=false --tx_power=8
        run_case_bg $seed very_heavy "04_gradpc"             $cv --enable_hello=true --enable_power_control=true
        run_case_bg $seed very_heavy "05_gradpc_prefer_low"  $cv --enable_hello=true --enable_power_control=true --prefer_low_power_channel=true
        run_case_bg $seed very_heavy "06_gradpc_no_chswitch" $cv --enable_hello=true --enable_power_control=true --enable_channel_switch_on_retry=false
    fi
}

# ── main ─────────────────────────────────────────────────────────────────────

BUILD_LABEL=$(if $USE_OPTIMIZED; then echo "optimized"; else echo "debug"; fi)
TOTAL=$(echo $SEEDS | wc -w)
CASES_PER_SEED=$(( NUM_NODES >= 100 ? 18 : 12 ))

echo "════════════════════════════════════════════════════════════"
echo "  Building $BUILD_LABEL binary..."
echo "════════════════════════════════════════════════════════════"
(cd "$ROOT_DIR" && $WAF_BUILD 2>&1) | tail -3

mkdir -p "$RESULTS_DIR"

echo "════════════════════════════════════════════════════════════"
echo "  Launching $((TOTAL * CASES_PER_SEED)) simulations in parallel  (seeds=$SEEDS)"
echo "════════════════════════════════════════════════════════════"

for seed in $SEEDS; do
    launch_seed "$seed"
done

if ! $DRY_RUN; then
    echo ""
    echo "  Waiting for all $((TOTAL * 12)) cases to finish..."
    wait
    echo ""
    echo "  All done."
    print_summary "$SEEDS"
    rm -rf "$RESULTS_DIR"
fi
