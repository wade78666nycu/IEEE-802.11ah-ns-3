#!/usr/bin/env bash

set -euo pipefail

root_dir="$(cd "$(dirname "$0")" && pwd)"
scenario_dir="$root_dir/output_file/rand"
cases_root="$scenario_dir/hello_power_cases"

copy_case_outputs() {
    local case_dir="$1"

    mkdir -p "$case_dir"

    local files=(
        aodv_rand_path.log
        rand_run_summary.txt
        rand_energy_consumption.txt
        rand_mac_retry_summary.txt
        rand_node_position.txt
        rand_node_power.txt
        rand_ett_ch1.csv
        rand_ett_ch2.csv
        rand_ett_ch3.csv
        rand_tx_energy.txt
        rand_tx_rate.txt
    )

    for file in "${files[@]}"; do
        if [[ -f "$scenario_dir/$file" ]]; then
            cp -f "$scenario_dir/$file" "$case_dir/$file"
        fi
    done

    if [[ -f "$case_dir/aodv_rand_path.log" ]]; then
        python3 "$root_dir/parse_path_detailed.py" \
            "$case_dir/aodv_rand_path.log" \
            "$case_dir/aodv_path_report.txt" \
            --scenario rand >/dev/null
    fi
}

run_case() {
    local case_key="$1"
    local label="$2"
    shift 2

    local case_dir="$cases_root/$case_key"

    mkdir -p "$case_dir"

    echo "============================================================"
    echo "$label"
    echo "Command: ./waf --run='rand-exp' --command='%s $*'"
    echo "============================================================"

    ./waf --run='rand-exp' --command="%s $*" 2>&1 | tee "$case_dir/stdout.log"
    copy_case_outputs "$case_dir"
    echo
}

extra_args=("$@")

mkdir -p "$cases_root"

run_case \
    "01_hello_on_power_on" \
    "1. enable_hello=true, enable_power_control=true" \
    --enable_hello=true \
    --enable_power_control=true \
    "${extra_args[@]}"

run_case \
    "02_hello_on_power_off" \
    "2. enable_hello=true, enable_power_control=false" \
    --enable_hello=true \
    --enable_power_control=false \
    "${extra_args[@]}"

run_case \
    "03_hello_off_power_off" \
    "3. enable_hello=false, enable_power_control=false" \
    --enable_hello=false \
    --enable_power_control=false \
    "${extra_args[@]}"

run_case \
    "04_hello_on_power_on_prefer_low_power" \
    "4. enable_hello=true, enable_power_control=true, prefer_low_power_channel=true" \
    --enable_hello=true \
    --enable_power_control=true \
    --prefer_low_power_channel=true \
    "${extra_args[@]}"

python3 "$root_dir/summarize_rand_hello_power_cases.py" \
    --cases-root "$cases_root" \
    --output "$cases_root/summary_report.txt"

echo "Summary report: $cases_root/summary_report.txt"
