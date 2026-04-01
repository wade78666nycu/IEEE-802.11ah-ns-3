#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import re
from collections import Counter
from pathlib import Path


CASE_ORDER = [
    ("01_hello_on_power_on", "1. hello=on, power_control=on"),
    ("02_hello_on_power_off", "2. hello=on, power_control=off"),
    ("03_hello_off_power_off", "3. hello=off, power_control=off"),
    (
        "04_hello_on_power_on_prefer_low_power",
        "4. hello=on, power_control=on, prefer_low_power=on",
    ),
]


def read_key_value_file(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    if not path.exists():
        return data
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
        elif ":" in line:
            key, value = line.split(":", 1)
        else:
            continue
        data[key.strip()] = value.strip()
    return data


def parse_energy(path: Path) -> float:
    if not path.exists():
        return 0.0
    match = re.search(r"Total Network Energy:\s+([\d.]+)", path.read_text(encoding="utf-8", errors="replace"))
    return float(match.group(1)) if match else 0.0


def parse_mac_retry(path: Path) -> dict[str, int]:
    data = read_key_value_file(path)
    return {
        "short": int(data.get("short_retry_count", "0")),
        "long": int(data.get("long_retry_count", "0")),
        "final_rts": int(data.get("final_rts_failed_count", "0")),
        "final_data": int(data.get("final_data_failed_count", "0")),
    }


def parse_aodv_overhead(path: Path) -> dict[str, Counter]:
    counters = {"tx": Counter(), "rx": Counter()}
    if not path.exists():
        return counters
    regex = re.compile(r"AODV_METRIC\s+(TX|RX)\s+type=(RREQ|RREP|RERR|RREP_ACK)")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = regex.search(line)
        if not match:
            continue
        direction = match.group(1).lower()
        packet_type = match.group(2)
        counters[direction][packet_type] += 1
    return counters


def parse_path_report(path: Path) -> dict[str, object]:
    selected_etts: list[float] = []
    hop_counts: list[int] = []
    next_hop_channel_counts: Counter[int] = Counter()
    hop_channel_counts: Counter[int] = Counter()

    if not path.exists():
        return {
            "discoveries": 0,
            "avg_selected_ett": 0.0,
            "avg_hop_count": 0.0,
            "next_hop_channels": next_hop_channel_counts,
            "hop_channels": hop_channel_counts,
        }

    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    discoveries = 0
    pending_selected_path = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("discovery #"):
            discoveries += 1

        match = re.search(r"selected cumulative ETT=([\d.]+).*next hop channel=(\d+)", stripped)
        if match:
            selected_etts.append(float(match.group(1)))
            next_hop_channel_counts[int(match.group(2))] += 1

        if stripped == "selected path:":
            pending_selected_path = True
            continue

        if pending_selected_path:
            pending_selected_path = False
            if "->" in stripped:
                nodes = [token.strip() for token in stripped.split("->") if token.strip()]
                hop_counts.append(max(0, len(nodes) - 1))

        match = re.search(r"channel=(\d+)", stripped)
        if match and "->" in stripped:
            hop_channel_counts[int(match.group(1))] += 1

    avg_selected_ett = sum(selected_etts) / len(selected_etts) if selected_etts else 0.0
    avg_hop_count = sum(hop_counts) / len(hop_counts) if hop_counts else 0.0
    return {
        "discoveries": discoveries,
        "avg_selected_ett": avg_selected_ett,
        "avg_hop_count": avg_hop_count,
        "next_hop_channels": next_hop_channel_counts,
        "hop_channels": hop_channel_counts,
    }


def parse_route_selected_log(path: Path) -> dict[str, object]:
    hop_counts: list[int] = []
    selected_etts: list[float] = []
    next_hop_channel_counts: Counter[int] = Counter()

    if not path.exists():
        return {
            "discoveries": 0,
            "avg_selected_ett": 0.0,
            "avg_hop_count": 0.0,
            "next_hop_channels": next_hop_channel_counts,
            "hop_channels": Counter(),
        }

    regex = re.compile(
        r"AODV_ROUTE_SELECTED mode=(\w+) src=([0-9.]+) dst=([0-9.]+) hop=(\d+) pathEtt=([\w.+-]+) .* nextHopChannel=(\d+)"
    )
    discoveries = 0
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = regex.search(line)
        if not match:
            continue
        discoveries += 1
        hop_counts.append(int(match.group(4)))
        path_ett_text = match.group(5)
        try:
            path_ett_value = float(path_ett_text)
        except ValueError:
            path_ett_value = 0.0
        selected_etts.append(path_ett_value)
        next_hop_channel_counts[int(match.group(6))] += 1

    avg_selected_ett = sum(selected_etts) / len(selected_etts) if selected_etts else 0.0
    avg_hop_count = sum(hop_counts) / len(hop_counts) if hop_counts else 0.0
    return {
        "discoveries": discoveries,
        "avg_selected_ett": avg_selected_ett,
        "avg_hop_count": avg_hop_count,
        "next_hop_channels": next_hop_channel_counts,
        "hop_channels": Counter(),
    }


def format_channel_counter(counter: Counter[int]) -> str:
    if not counter:
        return "ch1=0 ch2=0 ch3=0"
    return " ".join(f"ch{channel}={counter.get(channel, 0)}" for channel in (1, 2, 3))


def safe_float(data: dict[str, str], key: str) -> float:
    try:
        return float(data.get(key, "0"))
    except ValueError:
        return 0.0


def safe_int(data: dict[str, str], key: str) -> int:
    try:
        return int(float(data.get(key, "0")))
    except ValueError:
        return 0


def summarize_case(case_dir: Path, label: str) -> dict[str, object]:
    run_summary = read_key_value_file(case_dir / "rand_run_summary.txt")
    total_energy = parse_energy(case_dir / "rand_energy_consumption.txt")
    retry = parse_mac_retry(case_dir / "rand_mac_retry_summary.txt")
    overhead = parse_aodv_overhead(case_dir / "aodv_rand_path.log")
    path_metrics = parse_path_report(case_dir / "aodv_path_report.txt")
    if path_metrics["discoveries"] == 0:
        path_metrics = parse_route_selected_log(case_dir / "aodv_rand_path.log")

    delivered = safe_int(run_summary, "total_received_packets")
    energy_per_delivered = total_energy / delivered if delivered else math.inf
    return {
        "label": label,
        "case_dir": case_dir,
        "pdr_percent": safe_float(run_summary, "pdr_percent"),
        "throughput_bps": safe_float(run_summary, "throughput_bps"),
        "avg_delay_ms": safe_float(run_summary, "avg_delay_ms"),
        "max_delay_ms": safe_float(run_summary, "max_delay_ms"),
        "total_sent_packets": safe_int(run_summary, "total_sent_packets"),
        "total_received_packets": delivered,
        "total_energy_j": total_energy,
        "energy_per_delivered_packet": energy_per_delivered,
        "mac_retry": retry,
        "aodv_overhead": overhead,
        "path_metrics": path_metrics,
    }


def build_report(case_summaries: list[dict[str, object]]) -> str:
    lines: list[str] = []
    lines.append("Rand Hello/Power Comparison")
    lines.append("===========================")
    lines.append("")

    for summary in case_summaries:
        overhead = summary["aodv_overhead"]
        retry = summary["mac_retry"]
        path_metrics = summary["path_metrics"]
        energy_per_delivered = summary["energy_per_delivered_packet"]
        energy_per_delivered_text = (
            f"{energy_per_delivered:.4f}"
            if math.isfinite(energy_per_delivered)
            else "inf"
        )

        lines.append(summary["label"])
        lines.append(
            "  PDR={:.2f}%  throughput={:.2f} bps  energy={:.4f} J  energy/delivered={}".format(
                summary["pdr_percent"],
                summary["throughput_bps"],
                summary["total_energy_j"],
                energy_per_delivered_text,
            )
        )
        lines.append(
            "  delay(avg/max)={:.3f}/{:.3f} ms  recv={}/{}".format(
                summary["avg_delay_ms"],
                summary["max_delay_ms"],
                summary["total_received_packets"],
                summary["total_sent_packets"],
            )
        )
        lines.append(
            "  aodv tx: RREQ={} RREP={} RERR={} ACK={} | rx: RREQ={} RREP={} RERR={} ACK={}".format(
                overhead["tx"].get("RREQ", 0),
                overhead["tx"].get("RREP", 0),
                overhead["tx"].get("RERR", 0),
                overhead["tx"].get("RREP_ACK", 0),
                overhead["rx"].get("RREQ", 0),
                overhead["rx"].get("RREP", 0),
                overhead["rx"].get("RERR", 0),
                overhead["rx"].get("RREP_ACK", 0),
            )
        )
        lines.append(
            "  mac retry: short={} long={} final_rts={} final_data={}".format(
                retry["short"], retry["long"], retry["final_rts"], retry["final_data"]
            )
        )
        lines.append(
            "  path: discoveries={} avg_hops={:.2f} avg_path_ett={:.3f} next-hop-channels={} hop-channels={}".format(
                path_metrics["discoveries"],
                path_metrics["avg_hop_count"],
                path_metrics["avg_selected_ett"],
                format_channel_counter(path_metrics["next_hop_channels"]),
                format_channel_counter(path_metrics["hop_channels"]),
            )
        )
        lines.append("")

    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--cases-root",
        default="/home/wade/ah-lab/output_file/rand/hello_power_cases",
        help="Directory that contains per-case outputs.",
    )
    parser.add_argument(
        "--output",
        default="/home/wade/ah-lab/output_file/rand/hello_power_cases/summary_report.txt",
        help="Path to the combined summary report.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cases_root = Path(args.cases_root)
    summaries: list[dict[str, object]] = []
    for case_dir_name, label in CASE_ORDER:
        case_dir = cases_root / case_dir_name
        summaries.append(summarize_case(case_dir, label))

    report = build_report(summaries)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(report + "\n", encoding="utf-8")
    print(f"Summary written to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())