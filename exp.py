#!/usr/bin/python3

"""
    script to run `rand-exp` simulation with different random seeds
    it will save the result figure in png format and throughputs in pickle format
    data structure of throughput is dictionary of dictionary
    the format is:
    throughput[routing_method] -> [power_control_method] -> throughput
"""

from alive_progress import alive_bar
import time
import pathlib
import matplotlib.pyplot as plt
import subprocess
import pickle

"""
    recv_pkt_dict: total packets received of different power control method
    packet_size: packet size in Kbytes
    simulation_time: simulation time in seconds
    routing_method: DSR or AODV
    total_nodes: list of total node numbers
"""


def plot_result(
    recv_pkt_dict, packet_size, simulation_time, routing_method, total_nodes, seed
):
    # set figure size
    plt.rcParams["figure.figsize"] = (9, 7)
    line_colors = ["blue", "red", "lime", "orange", "magenta", "cyan"]
    markers = ["d", "s", "x", "o", "^", "v"]
    fig, ax = plt.subplots(layout="constrained")
    for (method, packet_num), line_color, mark in zip(
        recv_pkt_dict.items(), line_colors, markers
    ):
        thrput_per_sec = [
            round(packets * packet_size / simulation_time, 2) for packets in packet_num
        ]
        ax.plot(
            total_nodes, thrput_per_sec, color=line_color, marker=mark, label=method
        )

    seed_str = "seed " + str(seed) + " "
    title = seed_str + routing_method + " System Throughput"
    ax.set_title(title)
    ax.set_ylabel("KBytes / sec")
    ax.set_xlabel("Simulation Nodes")
    ax.legend(loc="upper right")

    return ax


def get_total_recv_packet(output_str) -> int:
    key_word = "Total received packets:"
    if key_word not in output_str:
        return 0
    return int(output_str[output_str.find(key_word) + len(key_word) :])


def run_simulation(total_nodes, routing_method, seed, recv_pkt_dict) -> bool:
    send_packet_num = int(1700)
    simulation_cmds = {
        "default with 1 channel": './waf --run="rand-exp" --command-template="%s --num_nodes={} --routing_method={} --seed={} --send_packet_num={} --use_gradpc=false --device_num=1"',
        "default with 3 channels": './waf --run="rand-exp" --command-template="%s --num_nodes={} --routing_method={} --seed={} --send_packet_num={} --use_gradpc=false"',
        "biconn": './waf --run="rand-exp" --command-template="%s --num_nodes={} --routing_method={} --seed={} --send_packet_num={} --use_gradpc=false --use_biconn=true"',
        "gradPC torque": './waf --run="rand-exp" --command-template="%s --num_nodes={} --routing_method={} --seed={} --send_packet_num={} --use_gradpc=true --gradpc_type=1"',
        "gradPC porprotional": './waf --run="rand-exp" --command-template="%s --num_nodes={} --routing_method={} --seed={} --send_packet_num={} --use_gradpc=true --gradpc_type=2"',
        "gradPC logarithm": './waf --run="rand-exp" --command-template="%s --num_nodes={} --routing_method={} --seed={} --send_packet_num={} --use_gradpc=true --gradpc_type=3"',
    }

    for power_control, cmd_str in simulation_cmds.items():
        recv_pkt_dict[routing_method][power_control] = []
        for nodes in total_nodes:
            cmd = cmd_str.format(nodes, routing_method, seed, send_packet_num)
            # print(cmd)
            process_result = subprocess.run(
                cmd, shell=True, capture_output=True, text=True
            )
            if process_result.returncode != 0:
                print(process_result.stderr)
                return False
            # print('nodes:{}'.format(nodes))
            # print(process_result)
            # ns-3 NS_LOG_INFO uses stderr
            total_recv_pkt = get_total_recv_packet(process_result.stderr)
            recv_pkt_dict[routing_method][power_control].append(total_recv_pkt)
    return True


def main():
    total_run = int(3)
    valid_run = int(0)
    seed = int(0)
    packet_size = 1  # Kbytes
    simulation_time = 60.0  # Seconds
    total_nodes = list(range(30, 101, 10))

    # define the directory path
    save_dict_dir = pathlib.Path("/home/alan/Documents/result-data/result-dict")
    save_pic_dir = pathlib.Path("/home/alan/Documents/result-data/result-pic")

    # check if the directory exists
    if not save_dict_dir.exists():
        # if it doesn't exist, create the directory
        save_dict_dir.mkdir(parents=True, exist_ok=True)
    if not save_pic_dir.exists():
        save_pic_dir.mkdir(parents=True, exist_ok=True)

    recv_pkt_dict = {"aodv": dict(), "dsr": dict()}
    with alive_bar(total_run) as bar:
        while valid_run < total_run:
            is_valid = True
            for routing_method in ["aodv", "dsr"]:
                recv_pkt_dict[routing_method].clear()
                if (
                    run_simulation(total_nodes, routing_method, seed, recv_pkt_dict)
                    == False
                ):
                    # not a valid simulation
                    is_valid = False
                    break
            if not is_valid:
                seed += 1
                continue

            valid_run += 1
            # plot the result
            routing_method = "aodv"
            ax = plot_result(
                recv_pkt_dict[routing_method],
                packet_size,
                simulation_time,
                routing_method,
                total_nodes,
                seed,
            )

            # save as pickle so it can be reloaded and modified later
            file_name = "seed{}-{}-result.pkl".format(seed, routing_method)
            # define the file path
            file_path = save_pic_dir / file_name
            with open(file_path, "wb") as f:
                pickle.dump(ax, f)
            # use pickle.load(open(file_name, 'rb')) to reload

            routing_method = "dsr"
            file_name = "seed{}-{}-result.pkl".format(seed, routing_method)
            file_path = save_pic_dir / file_name
            ax = plot_result(
                recv_pkt_dict[routing_method],
                packet_size,
                simulation_time,
                routing_method,
                total_nodes,
                seed,
            )
            with open(file_path, "wb") as f:
                pickle.dump(ax, f)

            # save as png
            # plt.savefig('seed{}-{}-result.png'.format(seed, routing_method), dpi=300)
            # NOTE: need to save to file before showing, else cannot be reloaded
            # plt.show()

            # save recv_pkt_dict
            file_name = "seed{}-recv_pkt_dict.pkl".format(seed)
            file_path = save_dict_dir / file_name
            with open(file_path, "wb") as f:
                pickle.dump(recv_pkt_dict, f)

            bar()
            seed += 1

    return


if __name__ == "__main__":
    main()
