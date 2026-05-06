#!/usr/bin/env bash
# Comparison script: 5 configurations × 2 traffic levels, all cases run in PARALLEL.
#
# Each case gets its own scenario_name so outputs don't collide.
# Results land in: output_file/comparison/{traffic}/{case_key}/
#
# Usage:
#   ./run_comparison.sh [--seed N] [--debug] [--dry-run]
#   --seed N    random seed (default: 9)
#   --debug     use debug build instead of optimized
#   --dry-run   print commands without running

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
CASES_ROOT="$ROOT_DIR/output_file/comparison"

SEED=9
USE_OPTIMIZED=false
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --seed)      SEED="$2"; shift 2 ;;
        --optimized) USE_OPTIMIZED=true; shift ;;
        --dry-run)   DRY_RUN=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Optimized build requires running build-optimize.sh first.
# Default is debug build which works out of the box.
if $USE_OPTIMIZED; then
    WAF_CMD="./waf --build-profile=optimized"
else
    WAF_CMD="./waf"
fi

# ── run one case in background ──────────────────────────────────────────────

run_case_bg() {
    local traffic="$1"
    local case_key="$2"
    local label="$3"
    shift 3

    # unique scenario_name → unique output directory
    local sname="cmp_${traffic}_${case_key}"
    local sim_out="$ROOT_DIR/output_file/${sname}"
    local case_dir="$CASES_ROOT/${traffic}/${case_key}"
    local log="$case_dir/stdout.log"

    mkdir -p "$case_dir"

    echo "  → [$traffic/$case_key] $label"

    if $DRY_RUN; then
        echo "    CMD: $WAF_CMD --run='rand-exp' --command=\"%s --scenario_name=$sname $*\""
        return
    fi

    (
        cd "$ROOT_DIR"
        $WAF_CMD --run='rand-exp' \
            --command="%s --scenario_name=$sname $*" \
            > "$log" 2>&1

        # copy outputs from sim dir to case dir
        if [[ -d "$sim_out" ]]; then
            cp -f "$sim_out"/* "$case_dir/" 2>/dev/null || true
        fi
        echo "  ✓ [$traffic/$case_key] done"
    ) &
}

# ── summary after all runs ───────────────────────────────────────────────────

extract() { grep "^${2}=" "${1}" 2>/dev/null | cut -d= -f2 || echo "N/A"; }

print_summary() {
    echo ""
    echo "══════════════════════════════════════════════════════════════════════════════════════════"
    printf "  %-12s %-28s %8s %10s %10s %8s %10s\n" "Traffic" "Case" "PDR(%)" "Thru(Kbps)" "AvgDly(ms)" "RERR" "Energy(J)"
    echo "  ──────────────────────────────────────────────────────────────────────────────────────"

    for traffic in light heavy; do
        for case_key in 01_std_aodv 02_full_power 03_low_power_fixed 04_gradpc 05_gradpc_prefer_low 06_gradpc_no_chswitch; do
            local summary="$CASES_ROOT/${traffic}/${case_key}/cmp_${traffic}_${case_key}_run_summary.txt"
            if [[ ! -f "$summary" ]]; then
                summary=$(ls "$CASES_ROOT/${traffic}/${case_key}"/*_run_summary.txt 2>/dev/null | head -1 || echo "")
            fi

            local pdr thru delay rerr energy
            pdr=$(extract    "$summary" "pdr_percent")
            thru=$(extract   "$summary" "throughput_bps")
            delay=$(extract  "$summary" "avg_delay_ms")
            rerr=$(extract   "$summary" "total_rerr_sent")
            energy=$(extract "$summary" "total_energy_j")

            if [[ "$thru" != "N/A" ]]; then
                thru=$(awk "BEGIN{printf \"%.2f\", $thru/1000}")
            fi
            if [[ "$energy" != "N/A" ]]; then
                energy=$(awk "BEGIN{printf \"%.4f\", $energy}")
            fi

            printf "  %-12s %-28s %8s %10s %10s %8s %10s\n" "$traffic" "$case_key" "$pdr" "$thru" "$delay" "$rerr" "$energy"
        done
    done
    echo ""
}

# ── main ─────────────────────────────────────────────────────────────────────

mkdir -p "$CASES_ROOT"

COMMON_LIGHT="--seed=$SEED --num_flows=7  --data_rate=30Kbps  --send_packet_num=500  --show_log=false --export_node_info=true"
COMMON_HEAVY="--seed=$SEED --num_flows=14 --data_rate=30Kbps --send_packet_num=500 --show_log=false --export_node_info=true"

BUILD_LABEL=$(if $USE_OPTIMIZED; then echo "optimized"; else echo "debug"; fi)
echo "════════════════════════════════════════════════════════════"
echo "  Launching all cases in parallel  (build=$BUILD_LABEL, seed=$SEED)"
echo "════════════════════════════════════════════════════════════"

# light traffic
run_case_bg light "01_std_aodv" \
    "Standard AODV (single-ch, hop-count)" \
    $COMMON_LIGHT --enable_hello=false --device_num=1

run_case_bg light "02_full_power" \
    "Multi-ch ETT, full power 15 dBm" \
    $COMMON_LIGHT --enable_hello=true --enable_power_control=false --tx_power=15

run_case_bg light "03_low_power_fixed" \
    "Multi-ch ETT, fixed low power 8 dBm" \
    $COMMON_LIGHT --enable_hello=true --enable_power_control=false --tx_power=8

run_case_bg light "04_gradpc" \
    "Multi-ch ETT + GradPC" \
    $COMMON_LIGHT --enable_hello=true --enable_power_control=true

run_case_bg light "05_gradpc_prefer_low" \
    "Multi-ch ETT + GradPC + prefer_low_power" \
    $COMMON_LIGHT --enable_hello=true --enable_power_control=true --prefer_low_power_channel=true

run_case_bg light "06_gradpc_no_chswitch" \
    "Multi-ch ETT + GradPC, no channel-switch on retry (control)" \
    $COMMON_LIGHT --enable_hello=true --enable_power_control=true --enable_channel_switch_on_retry=false

# heavy traffic
run_case_bg heavy "01_std_aodv" \
    "Standard AODV (single-ch, hop-count)" \
    $COMMON_HEAVY --enable_hello=false --device_num=1

run_case_bg heavy "02_full_power" \
    "Multi-ch ETT, full power 15 dBm" \
    $COMMON_HEAVY --enable_hello=true --enable_power_control=false --tx_power=15

run_case_bg heavy "03_low_power_fixed" \
    "Multi-ch ETT, fixed low power 8 dBm" \
    $COMMON_HEAVY --enable_hello=true --enable_power_control=false --tx_power=8

run_case_bg heavy "04_gradpc" \
    "Multi-ch ETT + GradPC" \
    $COMMON_HEAVY --enable_hello=true --enable_power_control=true

run_case_bg heavy "05_gradpc_prefer_low" \
    "Multi-ch ETT + GradPC + prefer_low_power" \
    $COMMON_HEAVY --enable_hello=true --enable_power_control=true --prefer_low_power_channel=true

run_case_bg heavy "06_gradpc_no_chswitch" \
    "Multi-ch ETT + GradPC, no channel-switch on retry (control)" \
    $COMMON_HEAVY --enable_hello=true --enable_power_control=true --enable_channel_switch_on_retry=false

if ! $DRY_RUN; then
    echo ""
    echo "  Waiting for all 12 cases to finish..."
    wait
    echo ""
    echo "  All done."
    print_summary
    echo "Results: $CASES_ROOT"
fi
