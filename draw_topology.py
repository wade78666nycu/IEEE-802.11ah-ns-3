#!/usr/bin/python3
import networkx as nx
import matplotlib.pyplot as plt
from math import sqrt, dist, log10
import argparse
from pathlib import Path


def get_args():
    parser = argparse.ArgumentParser(prog='draw_topology.py', description='topology arguments')
    parser.add_argument('--radius', '-r', default=0, type=float, required=False, help='transmission range')

    # This is the correct way to handle accepting multiple arguments.
    # '+' == 1 or more.
    # '*' == 0 or more.
    # '?' == 0 or 1.
    # To make the input integers
    parser.add_argument('--target_node', nargs='+', type=int, required=False)

    parser.add_argument("--save_figure", '-s', required=False, help="save result figure", action="store_true")
    #parser.add_argument("--flag", help="help", action="store_true")
    parser.add_argument(
        "--freq",
        "-f",
        #default=2400.0,  # 802.11b
        default=920.0,  # 802.11ah
        type=float,
        required=False,
        help="operating frequency of the protocol (MHz), default = 920",
    )
    parser.add_argument(
        "--cca_threshold",
        "-c",
        default=-92.0,  # dBm
        type=float,
        required=False,
        help="clear channel access sensitivity threshold, default = -96",
    )
    parser.add_argument(
        "--path_loss_exp",
        "-e",
        default=2.5,
        type=float,
        required=False,
        help="Path loss exponent, default = 2.5",
    )
    parser.add_argument(
        "--noise",
        default=7.0,  # dBm
        type=float,
        required=False,
        help="Loss (dB) in the Signal-to-Noise-Ratio",
    )
    parser.add_argument(
        "--position_only",
        action="store_true",
        help="set this if only drawing the node's position"
    )
    topology_group = parser.add_mutually_exclusive_group(required=False)
    topology_group.add_argument(
        "--grid",
        action="store_true",
        help="read node files from output_file/grid and save figures with grid prefix",
    )
    topology_group.add_argument(
        "--rand",
        action="store_true",
        help="read node files from output_file/rand and save figures with rand prefix",
    )
    return parser.parse_args()

def set_plot_options(node_size=3):
    options = {
        "font_size": 15,
        #"node_size": 12,
        "node_size": node_size,
        "node_color": "black",
        "linewidths": 1,
        "width": 0.8,
        #"edge_color": "blue",
    }
    return options

def get_node_position(file_name = "grid_node_position.txt"):
    node_position = {} 
    try:
        with open(file_name, 'r') as f:
            total_nodes = int(f.readline().split(':')[1])
            for node_id in range(total_nodes):
                position = []
                for item in f.readline().split(':')[1].split():
                    position.append(float(item))
                node_position[node_id] = position
    except FileNotFoundError:
        print("File {} not found.".format(file_name))
        exit(1)
    return node_position

def get_node_power(file_name = "grid_node_power.txt"):
    node_power = {}
    try:
        with open(file_name, 'r') as f:
            total_nodes = int(f.readline().split(':')[1])
            for node_id in range(total_nodes):
                power = []
                for item in f.readline().split(':')[1].split():
                    power.append(item)
                node_power[node_id] = power
    except FileNotFoundError:
        print("File {} not found.".format(file_name))
        exit(1)
    return node_power

# Function to convert from mW to dBm
def mW2dBm(mW):
    return 10.0 * log10(mW)


# Function to convert from dBm to mW
def dBm2mW(dBm):
    return 10 ** ((dBm) / 10.0)


# freq (MHz)
# ref_distance (meters)
# const_num calculation: refer to https://en.wikipedia.org/wiki/Free-space_path_loss
def calculate_reference_loss(freq, ref_distance=1):
    const_num = -27.55
    return 20 * log10(ref_distance) + 20 * log10(freq) + const_num

def calculate_tx_range(
    tx_power_dBm, cca_thr, freq=920, path_loss_exp=2.5, ref_distance=1.0
):
    # min_receive_threshold = -95.0  # dBm
    reference_loss = calculate_reference_loss(
        freq, ref_distance
    )
    tx_range = 10 ** (
        (tx_power_dBm - cca_thr - reference_loss) / (10.0 * path_loss_exp)
    )  # meters
    return tx_range

def add_edge(topology, node_id, node_position, tx_range, total_nodes):
    for i in range(total_nodes):
        if i == node_id:
            continue
        if dist(node_position[i], node_position[node_id]) <= tx_range:
            topology.add_edge(i, node_id, color='blue')
    return

def add_target_node_edge(topology, node_id, node_position, tx_range, total_nodes, target_node):
    for i in range(total_nodes):
        if i == node_id:
            continue
        if dist(node_position[i], node_position[node_id]) <= tx_range:
            if i in target_node or node_id in target_node:
                topology.add_edge(i, node_id, color='red')
            #else:
            #    topology.add_edge(i, node_id, color='blue')
    return

def resolve_io_paths(args):
    """
    Decide where to read node position/power files and how to name output figures.

    - default: read from current directory (grid_node_*.txt) and save as lab-<id>.png
    - --grid:  read from output_file/grid and save as grid-lab-<id>.png (in that folder)
    - --rand:  read from output_file/rand and save as rand-lab-<id>.png (in that folder)
    """
    script_dir = Path(__file__).resolve().parent
    if args.grid:
        base_dir = script_dir / "output_file" / "grid"
        prefix = "grid-lab"
        position_file = "grid_node_position.txt"
        power_file = "grid_node_power.txt"
    elif args.rand:
        base_dir = script_dir / "output_file" / "rand"
        prefix = "rand-lab"
        position_file = "rand_node_position.txt"
        power_file = "rand_node_power.txt"
    else:
        base_dir = script_dir
        prefix = "lab"
        position_file = "grid_node_position.txt"
        power_file = "grid_node_power.txt"

    return {
        "base_dir": base_dir,
        "prefix": prefix,
        "position_path": base_dir / position_file,
        "power_path": base_dir / power_file,
    }

def main():
    args = get_args()
    print("operating frequency: {} MHz".format(args.freq))
    print("path loss exponent: {}".format(args.path_loss_exp))
    print("cca threshold: {}".format(args.cca_threshold))
    print("-" * 60)

    io_paths = resolve_io_paths(args)
    print("node position file: {}".format(io_paths["position_path"]))
    if not args.position_only:
        print("node power file: {}".format(io_paths["power_path"]))
    print("figure name prefix: {}".format(io_paths["prefix"]))
    print("-" * 60)

    options = set_plot_options()
    if args.position_only:
        options = set_plot_options(8)
    node_position = get_node_position(str(io_paths["position_path"]))
    total_nodes = len(node_position)

    # add nodes
    if args.position_only:
        topology = nx.Graph()
        topology.add_nodes_from(range(total_nodes))
        if args.target_node:
            color_map = ['red' if node in args.target_node  else 'black' for node in topology]
            node_size_map = [30 if node in args.target_node  else 3 for node in topology]
            options['node_color'] = color_map
            options['node_size'] = node_size_map
        #nx.draw(topology, node_position, with_labels=True, **options)
        nx.draw(topology, node_position, with_labels=True, **options)
        if args.save_figure:
            out_path = io_paths["base_dir"] / "{}.png".format(io_paths["prefix"])
            plt.savefig(str(out_path), dpi=300)
        plt.show()
        return

    node_power = get_node_power(str(io_paths["power_path"]))
    tx_power_range = {}
    power_list = set()
    for _, list_item in node_power.items():
        for power in list_item:
            power_list.add(power)
    for tx_power_dBm in power_list:
       tx_power_range[tx_power_dBm] = calculate_tx_range(
        float(tx_power_dBm), args.cca_threshold, args.freq, args.path_loss_exp
    )

    total_device = len(node_power[0])
    for device_id in range(total_device):
        plt.figure()
        # add nodes
        topology = nx.Graph()
        topology.add_nodes_from(range(total_nodes))

        # add edges
        for node_id in range(total_nodes):
            tx_range = tx_power_range[node_power[node_id][device_id]]
            if args.target_node:
                # draw edges from target_node only
                add_target_node_edge(topology, node_id, node_position, tx_range, total_nodes, args.target_node)
            else:
                add_edge(topology, node_id, node_position, tx_range, total_nodes)
        edges = topology.edges()
        edge_colors = [topology[u][v]['color'] for u,v in edges]
        nx.draw(topology, node_position, with_labels=True, edge_color = edge_colors, **options)
        if args.save_figure:
            # save figure
            out_path = io_paths["base_dir"] / "{}-{}.png".format(io_paths["prefix"], device_id)
            plt.savefig(str(out_path), dpi=300)
        plt.show()

    #input('press any key to continue') # to keep figure staying

    return

if __name__ == '__main__':
    main()
