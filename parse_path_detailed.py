#!/usr/bin/env python3
"""Parse AODV logs and reconstruct selected ETT-based paths.

Supports both grid and rand experiments via --scenario.
Use --all to include every AODV route discovery.
"""

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path


NODE_RE = re.compile(r"\[node (\d+)\]")
TIME_RE = re.compile(r"^(\d+\.\d+)s")
RREQ_RE = re.compile(
    r"RecvRequest ETT accumulate: origin=([0-9.]+) id=(\d+) sender=([0-9.]+) "
    r"bestChannel=(\d+) bestLinkEtt=([\d.]+) cumulative\(before=([\d.]+), after=([\d.]+)\)"
)
SELECTED_RE = re.compile(
    r"Destination delayed-reply timer fired: key=([0-9.]+)\|(\d+) "
    r"selected cumulative ETT=([\d.eE+\-]+) nextHop=([0-9.]+)(?: nextHopChannel=(\d+))?"
)
PUSH_RE = re.compile(r"\b(src_node_vec|dst_node_vec)\.emplace_back\((\d+)\)")
INIT_RE = re.compile(r"\b(src_node_vec|dst_node_vec)\s*=\s*\{([^}]*)\}", re.S)


def strip_cpp_comments(text):
    result = []
    index = 0
    in_block_comment = False

    while index < len(text):
        if in_block_comment:
            end = text.find("*/", index)
            if end == -1:
                break
            index = end + 2
            in_block_comment = False
            continue

        if text.startswith("/*", index):
            in_block_comment = True
            index += 2
            continue

        if text.startswith("//", index):
            newline = text.find("\n", index)
            if newline == -1:
                break
            result.append("\n")
            index = newline + 1
            continue

        result.append(text[index])
        index += 1

    return "".join(result)


def load_main_flows(config_file):
    path = Path(config_file)
    if not path.exists():
        return None

    src_nodes = []
    dst_nodes = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        content = strip_cpp_comments(handle.read())

    for match in PUSH_RE.finditer(content):
        vector_name, node_id = match.groups()
        if vector_name == "src_node_vec":
            src_nodes.append(int(node_id))
        else:
            dst_nodes.append(int(node_id))

    for match in INIT_RE.finditer(content):
        vector_name, values = match.groups()
        node_ids = [int(token) for token in re.findall(r"\d+", values)]
        if vector_name == "src_node_vec":
            src_nodes = node_ids
        else:
            dst_nodes = node_ids

    if not src_nodes or len(src_nodes) != len(dst_nodes):
        return None

    return set(zip(src_nodes, dst_nodes))


def ip_to_node_id(ip_str):
    parts = ip_str.split(".")
    if len(parts) != 4:
        return -1
    return int(parts[3]) - 1


def parse_prefix(line):
    node_match = NODE_RE.search(line)
    time_match = TIME_RE.search(line)
    return {
        "node_id": int(node_match.group(1)) if node_match else None,
        "time": float(time_match.group(1)) if time_match else None,
    }


def parse_log(log_file):
    rreq_graph = defaultdict(list)
    selected_path = {}

    with open(log_file, "r", encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            prefix = parse_prefix(line)

            rreq_match = RREQ_RE.search(line)
            if rreq_match and prefix["node_id"] is not None:
                origin_ip = rreq_match.group(1)
                rreq_id = rreq_match.group(2)
                sender_ip = rreq_match.group(3)
                key = (origin_ip, rreq_id)
                rreq_graph[key].append(
                    {
                        "line_no": line_no,
                        "time": prefix["time"],
                        "recv_node": prefix["node_id"],
                        "sender_ip": sender_ip,
                        "sender_node": ip_to_node_id(sender_ip),
                        "channel": int(rreq_match.group(4)),
                        "best_link_ett": float(rreq_match.group(5)),
                        "cumulative_before": float(rreq_match.group(6)),
                        "cumulative_after": float(rreq_match.group(7)),
                    }
                )
                continue

            selected_match = SELECTED_RE.search(line)
            if selected_match and prefix["node_id"] is not None:
                origin_ip = selected_match.group(1)
                rreq_id = selected_match.group(2)
                key = (origin_ip, rreq_id)
                selected_path[key] = {
                    "dst_node": prefix["node_id"],
                    "selected_ett": float(selected_match.group(3)),
                    "next_hop_ip": selected_match.group(4),
                    "next_hop_id": ip_to_node_id(selected_match.group(4)),
                    "next_hop_channel": int(selected_match.group(5)) if selected_match.group(5) else None,
                    "line_no": line_no,
                    "time": prefix["time"],
                }

    return rreq_graph, selected_path


def format_position(entry):
    if entry["time"] is not None:
        return f"t={entry['time']:.4f}s"
    return f"line={entry['line_no']}"


def find_selected_hop(all_hops, dst_node, selected_ett):
    dst_candidates = [hop for hop in all_hops if hop["recv_node"] == dst_node]
    if not dst_candidates:
        return None, []

    selected_hop = min(
        dst_candidates,
        key=lambda hop: (abs(hop["cumulative_after"] - selected_ett), hop["line_no"]),
    )
    return selected_hop, dst_candidates


def backtrack_path_entries(all_hops, selected_hop):
    if not selected_hop:
        return []

    path_entries = [selected_hop]
    current = selected_hop

    while current["cumulative_before"] > 0:
        prev_candidates = [
            hop
            for hop in all_hops
            if hop["recv_node"] == current["sender_node"]
            and abs(hop["cumulative_after"] - current["cumulative_before"]) < 0.05
            and hop["line_no"] < current["line_no"]
        ]
        if not prev_candidates:
            break

        current = min(
            prev_candidates,
            key=lambda hop: (abs(hop["cumulative_after"] - path_entries[-1]["cumulative_before"]), -hop["line_no"]),
        )
        path_entries.append(current)

    path_entries.reverse()
    return path_entries


def build_node_path(path_entries):
    if not path_entries:
        return []

    node_path = [path_entries[0]["sender_node"]]
    for hop in path_entries:
        node_path.append(hop["recv_node"])
    return node_path


def build_analyses(rreq_graph, selected_path):
    analyses = []

    for (origin_ip, rreq_id), selected in sorted(selected_path.items()):
        key = (origin_ip, rreq_id)
        all_hops = rreq_graph.get(key, [])
        src_node = ip_to_node_id(origin_ip)
        dst_node = selected["dst_node"]
        selected_ett = selected["selected_ett"]

        selected_hop, dst_candidates = find_selected_hop(all_hops, dst_node, selected_ett)
        path_entries = backtrack_path_entries(all_hops, selected_hop)
        node_path = build_node_path(path_entries)

        analyses.append(
            {
                "origin_ip": origin_ip,
                "rreq_id": int(rreq_id),
                "src_node": src_node,
                "dst_node": dst_node,
                "selected": selected,
                "selected_hop": selected_hop,
                "dst_candidates": dst_candidates,
                "path_entries": path_entries,
                "node_path": node_path,
            }
        )

    return analyses


def build_report(rreq_graph, selected_path, allowed_pairs=None):
    if not selected_path:
        return "找不到目的地延迟选路日志。请确认日志仍包含 'Destination delayed-reply timer fired'。\n"

    analyses = build_analyses(rreq_graph, selected_path)
    grouped_analyses = defaultdict(list)
    for analysis in analyses:
        pair = (analysis["src_node"], analysis["dst_node"])
        if allowed_pairs is not None and pair not in allowed_pairs:
            continue
        grouped_analyses[(analysis["src_node"], analysis["dst_node"])].append(analysis)

    if not grouped_analyses:
        return "找不到符合主 flow 的选路日志。\n"

    lines = []
    lines.append("=" * 100)
    lines.append("AODV path analysis")
    lines.append("=" * 100)

    for (src_node, dst_node), pair_analyses in sorted(grouped_analyses.items()):
        lines.append("")
        lines.append(f"source={src_node} destination={dst_node}")
        lines.append(f"discoveries={len(pair_analyses)}")

        for index, analysis in enumerate(sorted(pair_analyses, key=lambda item: item["rreq_id"]), start=1):
            selected = analysis["selected"]
            selected_hop = analysis["selected_hop"]
            dst_candidates = analysis["dst_candidates"]
            node_path = analysis["node_path"]
            path_entries = analysis["path_entries"]

            lines.append("")
            lines.append(f"  discovery #{index} (rreq_id={analysis['rreq_id']})")
            lines.append(
                f"  selected cumulative ETT={selected['selected_ett']:.3f}, "
                f"next hop={selected['next_hop_id']}, next hop channel={selected['next_hop_channel']}"
            )

            lines.append("")
            lines.append("  selected path:")
            if node_path:
                lines.append(f"  {' -> '.join(str(node) for node in node_path)}")
            else:
                lines.append("  unable to fully reconstruct path from current log")

            if dst_candidates:
                lines.append("")
                lines.append("  candidates reaching destination:")
                lines.append("  position      sender  channel  link_ett  cumulative  selected")
                for hop in sorted(dst_candidates, key=lambda item: (item["cumulative_after"], item["line_no"])):
                    chosen = "yes" if hop is selected_hop else ""
                    lines.append(
                        f"  {format_position(hop):<13} {hop['sender_node']:<7} {hop['channel']:<8} "
                        f"{hop['best_link_ett']:<9.3f} {hop['cumulative_after']:<11.3f} {chosen}"
                    )

            if path_entries:
                lines.append("")
                lines.append("  path breakdown:")
                for hop in path_entries:
                    lines.append(
                        f"  {hop['sender_node']} -> {hop['recv_node']}  "
                        f"link_ett={hop['best_link_ett']:.3f}  cumulative={hop['cumulative_after']:.3f}  "
                        f"channel={hop['channel']}  {format_position(hop)}"
                    )

    lines.append("")
    return "\n".join(lines)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log_file", nargs="?", default=None)
    parser.add_argument("report_file", nargs="?", default=None)
    parser.add_argument(
        "--scenario",
        choices=["grid", "rand"],
        default="grid",
        help="Experiment scenario for default input/output files.",
    )
    parser.add_argument(
        "--config",
        default=None,
        help="Path to config file used to discover main src/dst flow pairs.",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Include every discovered AODV route instead of only configured main flows.",
    )
    return parser.parse_args(argv)


def apply_scenario_defaults(args):
    root = Path("/home/wade/ah-lab/output_file") / args.scenario

    if args.log_file is None:
        args.log_file = str(root / f"aodv_{args.scenario}_path.log")

    if args.report_file is None:
        args.report_file = str(root / "aodv_path_report.txt")

    if args.config is None:
        if args.scenario == "grid":
            args.config = str(root / "scratch/grid-exp/grid-exp.cc")
        else:
            # rand src/dst pairs are generated via assign-src-dst-pair;
            # if static flow extraction fails, script falls back to include all discoveries.
            args.config = str(root / "scratch/rand-exp/rand-exp.cc")

    return args


def main(argv=None):
    args = apply_scenario_defaults(parse_args(argv or sys.argv[1:]))
    log_file = args.log_file
    report_file = args.report_file
    rreq_graph, selected_path = parse_log(log_file)
    allowed_pairs = None if args.all else load_main_flows(args.config)
    report = build_report(rreq_graph, selected_path, allowed_pairs=allowed_pairs)
    with open(report_file, "w", encoding="utf-8") as handle:
        handle.write(report)
    print(f"Report written to {report_file}")
    raise SystemExit(0)


if __name__ == "__main__":
    main()
