#!/usr/bin/bash
set -e # exits if error occurs
packet_num=1800

# if no arguments provided, use aodv
: '
routing_method="aodv"
if [ $# -ne 0 ]
then
    routing_method=$1
fi
echo routing-method: $routing_method
'

# $1 is the frequency
# $2 is the alarm duration in milliseconds
finish_alarm() {
  ( speaker-test --frequency $1 --test sine )& pid=$!;
  sleep 0.${2}s;
  kill -9 $pid;
}

run_simulation() {
    routing_method=$1
    seed=10
    local simulation_cmds=(
        "./waf --run=rand-exp --command-template=\"%s --num_nodes=\$nodes --seed=$seed --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=false --device_num=1\""
        "./waf --run=rand-exp --command-template=\"%s --num_nodes=\$nodes --seed=$seed --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=false\""
        "./waf --run=rand-exp --command-template=\"%s --num_nodes=\$nodes --seed=$seed --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=false --use_biconn=true\""
        "./waf --run=rand-exp --command-template=\"%s --num_nodes=\$nodes --seed=$seed --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=true --gradpc_type=1\""
        "./waf --run=rand-exp --command-template=\"%s --num_nodes=\$nodes --seed=$seed --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=true --gradpc_type=2\""
        "./waf --run=rand-exp --command-template=\"%s --num_nodes=\$nodes --seed=$seed --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=true --gradpc_type=3\""
    )

    for cmd in "${simulation_cmds[@]}"; do
        trap 'printf "%s\n" "Ctr + C pressed"; exit 2' SIGINT SIGTERM
        for nodes in $(seq 30 10 100); do
        #for nodes in $(seq 90 10 90); do
            echo nodes: $nodes
            eval "$cmd"
        done
    done
}

run_simulation aodv
run_simulation dsr
