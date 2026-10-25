#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0


import json
import math
import re

from collect_opencl_stats import METRICS, validate_metrics


METRIC_ORDER = list(METRICS)
DEFAULT_PLOT_METRICS = ("active_cycles", "ipc", "total_instret")


def validate_comparison(baseline, current):
    if any(
        result.get("schema_version") != 2 or not result.get("suite")
        for result in (baseline, current)
    ):
        raise SystemExit(
            "Incompatible legacy perf baseline; regenerate the main baseline with the curated suite"
        )
    if baseline["suite"] != current["suite"]:
        raise SystemExit(
            "Perf case set, inputs, configuration or SM differ; regenerate the main baseline"
        )
    for result in (baseline, current):
        if set(result.get("metrics", {})) != set(result["suite"]["cases"]):
            raise SystemExit("Missing or unmatched perf cases; comparison refused")
        validate_metrics(result["metrics"])


def format_value(value):
    if value is None:
        return "N/A"
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def format_delta(base, current):
    if base is None or current is None:
        return "N/A"
    if base == 0:
        return "+0.00%" if current == 0 else "N/A"
    return f"{((current - base) / base) * 100:+.2f}%"


def markdown_table(baseline, current, selected_metric=None):
    validate_comparison(baseline, current)
    if selected_metric:
        comparable_deltas(baseline, current, selected_metric)
    baseline_metrics = baseline.get("metrics", {})
    current_metrics = current.get("metrics", {})
    tests = sorted(set(baseline_metrics) | set(current_metrics))

    lines = [
        "# OpenCL Performance Compared With main",
        "",
        f"- baseline commit: `{baseline.get('commit') or 'unknown'}`",
        f"- current commit: `{current.get('commit') or 'unknown'}`",
        "",
        "| test | metric | main | current | delta |",
        "| --- | --- | ---: | ---: | ---: |",
    ]

    for test in tests:
        base_test = baseline_metrics.get(test, {})
        current_test = current_metrics.get(test, {})
        metrics = (
            [selected_metric]
            if selected_metric
            else [m for m in METRIC_ORDER if m in base_test or m in current_test]
        )
        for metric in metrics:
            base_value = base_test.get(metric)
            current_value = current_test.get(metric)
            lines.append(
                "| "
                f"{test} | {metric} | {format_value(base_value)} | "
                f"{format_value(current_value)} | {format_delta(base_value, current_value)} |"
            )

    lines.append("")
    lines.append(
        "Positive delta means the numeric metric increased; negative delta means it decreased."
    )
    return "\n".join(lines) + "\n"


def comparable_deltas(baseline, current, metric):
    validate_comparison(baseline, current)
    baseline_metrics = baseline.get("metrics", {})
    current_metrics = current.get("metrics", {})
    rows = []
    for test in sorted(set(baseline_metrics) | set(current_metrics)):
        base_value = baseline_metrics.get(test, {}).get(metric)
        current_value = current_metrics.get(test, {}).get(metric)
        if any(
            metric not in values
            or (
                values[metric] is not None
                and (
                    type(values[metric]) not in (int, float)
                    or not math.isfinite(values[metric])
                )
            )
            for values in (baseline_metrics[test], current_metrics[test])
        ):
            raise SystemExit(
                f"{test}: missing or nonnumeric metric {metric}; recollect both results from stats"
            )
        if base_value is None or current_value is None:
            continue  # Present numeric stats with undefined values are stored as null.
        if base_value == 0 and current_value != 0:
            continue
        rows.append(
            (
                test,
                base_value,
                current_value,
                ((current_value - base_value) / base_value) * 100 if base_value else 0,
            )
        )
    return rows


def write_plot(baseline, current, output, metric):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    rows = comparable_deltas(baseline, current, metric)
    output.parent.mkdir(parents=True, exist_ok=True)

    if not rows:
        fig, ax = plt.subplots(figsize=(10, 4))
        ax.axis("off")
        ax.text(
            0.5,
            0.55,
            "OpenCL performance comparison unavailable",
            ha="center",
            va="center",
            fontsize=16,
            weight="bold",
        )
        ax.text(
            0.5,
            0.40,
            "Percentage delta is undefined for zero baselines or undefined metric values.",
            ha="center",
            va="center",
            fontsize=11,
        )
        fig.savefig(output, dpi=160, bbox_inches="tight")
        plt.close(fig)
        return

    rows.sort(key=lambda row: row[3])
    tests = [row[0] for row in rows]
    deltas = [row[3] for row in rows]
    colors = ["#2a9d8f" if delta < 0 else "#e76f51" for delta in deltas]
    height = max(5, min(18, 0.36 * len(rows) + 2.5))

    fig, ax = plt.subplots(figsize=(11, height))
    y_positions = range(len(rows))
    ax.barh(y_positions, deltas, color=colors)
    ax.axvline(0, color="#333333", linewidth=0.8)
    ax.set_yticks(list(y_positions), tests)
    ax.set_xlabel(f"{metric} delta vs main (%)")
    ax.set_title("OpenCL Performance Compared With main", pad=28)
    ax.grid(axis="x", linestyle="--", alpha=0.25)

    baseline_commit = baseline.get("commit") or "unknown"
    current_commit = current.get("commit") or "unknown"
    ax.text(
        0,
        1.01,
        f"baseline: {baseline_commit[:12]}    current: {current_commit[:12]}",
        transform=ax.transAxes,
        ha="left",
        va="bottom",
        fontsize=9,
        color="#555555",
    )

    max_abs = max(abs(delta) for delta in deltas)
    pad = max(1.0, max_abs * 0.12)
    ax.set_xlim(min(deltas) - pad, max(deltas) + pad)

    for y, delta in zip(y_positions, deltas):
        x_offset = pad * 0.15
        if delta >= 0:
            ax.text(
                delta + x_offset,
                y,
                f"{delta:+.2f}%",
                va="center",
                ha="left",
                fontsize=8,
            )
        else:
            ax.text(
                delta - x_offset,
                y,
                f"{delta:+.2f}%",
                va="center",
                ha="right",
                fontsize=8,
            )

    fig.tight_layout()
    fig.savefig(output, dpi=160)
    plt.close(fig)


def compare(baseline_path, current_path, output, metric=None):
    baseline = json.loads(baseline_path.read_text())
    current = json.loads(current_path.read_text())
    report = markdown_table(baseline, current, metric)
    output.mkdir(parents=True, exist_ok=True)
    (output / "comparison.md").write_text(report)
    for name in (metric,) if metric else DEFAULT_PLOT_METRICS:
        write_plot(
            baseline, current, output / (re.sub(r"[^\w.-]", "_", name) + ".png"), name
        )
    print(f"Comparison: {output}")
