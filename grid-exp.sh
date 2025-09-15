#!/usr/bin/bash
set -e # exits on error

packet_num=1800
# if no arguments provided, use aodv
routing_method="aodv"
if [ $# -ne 0 ]
then
    routing_method=$1
fi
echo routing-method: $routing_method

for nodes in 64 
#for nodes in 25 36 49 64 81 100
do
trap 'printf "%s\n" "Ctr + C pressed"; exit 2' SIGINT SIGTERM
echo nodes: $nodes
./waf --run="grid-exp" --command-template="%s --num_nodes=$nodes --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=false --device_num=1"
./waf --run="grid-exp" --command-template="%s --num_nodes=$nodes --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=false"
#./waf --run="grid-exp" --command-template="%s --num_nodes=$nodes --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=false --use_biconn=true"
./waf --run="grid-exp" --command-template="%s --num_nodes=$nodes --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=true --gradpc_type=1"
./waf --run="grid-exp" --command-template="%s --num_nodes=$nodes --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=true --gradpc_type=2"
./waf --run="grid-exp" --command-template="%s --num_nodes=$nodes --routing_method=$routing_method --send_packet_num=$packet_num --use_gradpc=true --gradpc_type=3"
done
