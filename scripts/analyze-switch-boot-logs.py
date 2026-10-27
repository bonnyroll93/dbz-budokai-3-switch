"""Read a captured Switch log directory; emit a reproducible JSON summary.

No console access or file writes. Presentation rate and sampled image changes
are deliberately separate. SPU CPU ratios exclude WAIT_SIGNAL; function
samples are wall samples and do not establish whole-console CPU utilization.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re


def read(folder, name):
    path = folder / name
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def records(text, required):
    for line in text.splitlines():
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if not all(key in fields for key in required):
            continue
        try:
            yield {key: int(fields[key], 0) for key in required}
        except ValueError:
            continue


def describe_rates(values, threshold=None):
    if not values:
        return None
    result = {
        "count": len(values),
        "min": round(min(values), 3),
        "max": round(max(values), 3),
        "mean": round(sum(values) / len(values), 3),
    }
    if threshold is not None:
        result[f"windows_below_{threshold:g}"] = sum(value < threshold for value in values)
    return result


def analyze(folder):
    stderr = read(folder, "t19-stderr.log")
    marker = re.search(r"\[t19-build\] ([^\r\n]+)", stderr)
    compiled = re.search(r"\[build\] compiled ([^\r\n]+)", stderr)
    ppu_sampler = re.search(r"\[t23\] PPU hot-address sampler (enabled|unavailable)", stderr)
    ppu_hot_path = folder / "t23-ppu-hot.log"
    ppu_hot_bytes = ppu_hot_path.stat().st_size if ppu_hot_path.is_file() else 0
    clocks = list(records(read(folder, "t21-clock.log"), ("frames", "delta", "wall_ms")))
    total_ms = sum(row["wall_ms"] for row in clocks)
    first = clocks[:7]
    result = {
        "directory": str(folder),
        "build": marker.group(1) if marker else None,
        "compiled_at": compiled.group(1) if compiled else None,
        "neon_selftest_pass": "SPU shufb NEON self-test PASS" in stderr,
        "function_profiler_enabled": "SPU function profiler enabled" in stderr,
        "ppu_sampler_status": ppu_sampler.group(1) if ppu_sampler else "not_started_or_unreported",
        "ppu_hot_log_bytes": ppu_hot_bytes,
        "ppu_hot_log_freshness": (
            "current_run" if ppu_sampler and ppu_sampler.group(1) == "enabled"
            else "stale_or_unrelated" if ppu_hot_bytes
            else "not_produced"
        ),
        "clock_windows": len(clocks),
        "presentation_rate_hz": round(sum(row["delta"] for row in clocks) * 1000 / total_ms, 3) if total_ms else None,
        "first_7_windows_mean_ms": round(sum(row["wall_ms"] for row in first) / len(first), 3) if first else None,
    }
    loop_log = read(folder, "t26-loop-prof.log")
    loop_blocks = re.split(r"(?=^window_ms=)", loop_log, flags=re.MULTILINE)
    loop_rows = []
    for block in loop_blocks:
        header = re.search(r"^window_ms=(\d+) passes=(\d+) pass_avg_us=(\d+)", block, re.MULTILINE)
        if not header or int(header.group(1)) == 0:
            continue
        logic = re.search(r"logic60 steps/s=([0-9.]+)", block)
        du_skipped = re.search(r"du-render-skipped=(\d+)", block)
        tasks = {}
        for task in re.finditer(
                r"task fn=(0x[0-9A-Fa-f]+).*?runs/pass=([0-9.]+) avg_us=\s*(\d+) max_us=\s*(\d+)"
                r" gcm_runs=(\d+) gcm_kb/pass=([0-9.]+) extra/pass=([0-9.]+)"
                r" extra_avg_us=\s*(\d+)", block):
            tasks[task.group(1).upper()] = {
                "runs_per_pass": float(task.group(2)),
                "avg_us": int(task.group(3)),
                "max_us": int(task.group(4)),
                "gcm_runs": int(task.group(5)),
                "gcm_kb_per_pass": float(task.group(6)),
                "extra_per_pass": float(task.group(7)),
                "extra_avg_us": int(task.group(8)),
            }
        loop_rows.append({
            "window_us": int(header.group(1)),
            "passes": int(header.group(2)),
            "pass_rate_hz": int(header.group(2)) * 1_000_000 / int(header.group(1)),
            "logic60_hz": float(logic.group(1)) if logic else None,
            "du_render_skipped": int(du_skipped.group(1)) if du_skipped else 0,
            "tasks": tasks,
        })
    pass_rates = [row["pass_rate_hz"] for row in loop_rows]
    logic_rates = [row["logic60_hz"] for row in loop_rows if row["logic60_hz"] is not None]
    scene_functions = sorted(set(re.findall(
        r"scene_fn=(0x[0-9A-Fa-f]+)", loop_log)))
    task_functions = sorted(set(re.findall(r"task fn=(0x[0-9A-Fa-f]+)", loop_log)))
    task_function_set = {value.upper() for value in task_functions}
    targets = {
        "du_map": "0X001DBC58",
        "level_screen": "0X001D1604",
        "combat_task": "0X000D1490",
    }
    target_windows = {}
    for name, fn in targets.items():
        rows = [(index, row, row["tasks"][fn]) for index, row in enumerate(loop_rows, 1)
                if fn in row["tasks"]]
        stats = {
            "windows": len(rows),
            "window_numbers": [index for index, _, _ in rows],
            "pass_rate_hz": describe_rates([row["pass_rate_hz"] for _, row, _ in rows], 30),
            "logic60_steps_per_second": describe_rates(
                [row["logic60_hz"] for _, row, _ in rows if row["logic60_hz"] is not None]),
            "task_avg_us": describe_rates([task["avg_us"] for _, _, task in rows]),
            "task_max_us": max((task["max_us"] for _, _, task in rows), default=None),
            "task_runs_per_pass": describe_rates([task["runs_per_pass"] for _, _, task in rows]),
            "task_gcm_kb_per_pass": describe_rates([task["gcm_kb_per_pass"] for _, _, task in rows]),
            "task_extra_per_pass": describe_rates([task["extra_per_pass"] for _, _, task in rows]),
            "task_extra_avg_us": describe_rates([task["extra_avg_us"] for _, _, task in rows]),
        }
        if name == "du_map":
            stats["du_render_skipped_total"] = sum(
                row["du_render_skipped"] for _, row, _ in rows)
        target_windows[name] = stats
    result["main_loop_profile"] = {
        "windows": len(loop_rows),
        "pass_rate_hz": describe_rates(pass_rates, 30),
        "logic60_steps_per_second": describe_rates(logic_rates),
        "logic_windows_59_to_61_hz": sum(59 <= value <= 61 for value in logic_rates),
        "scene_functions": scene_functions,
        "task_function_count": len(task_functions),
        "target_tasks_seen": {
            "du_map_0x001DBC58": "0X001DBC58" in task_function_set,
            "level_screen_0x001D1604": "0X001D1604" in task_function_set,
            "combat_task_0x000D1490": "0X000D1490" in task_function_set,
        },
        "target_task_windows": target_windows,
    }
    draw_windows = []
    for line in read(folder, "t23-draw-prof.log").splitlines():
        header = re.search(r"window_ms=([0-9.]+) frames=(\d+) draws=(\d+)", line)
        if not header or float(header.group(1)) <= 0 or int(header.group(2)) <= 0:
            continue
        vc_hashed = re.search(r"\bvc_hashed_kb=(\d+)", line)
        texture_counts = {
            key: int(value) for key, value in re.findall(
                r"\b(tex_lookup_calls|tex_lookup_hits|tex_lookup_entries|tex_uploads)=(\d+)", line)
        }
        draw_windows.append({
            "window_ms": float(header.group(1)),
            "frames": int(header.group(2)),
            "draws": int(header.group(3)),
            "vc_hashed_kb": int(vc_hashed.group(1)) if vc_hashed else None,
            "texture_counts": texture_counts,
            "cpu_stages_ms": {
                key: float(value) for key, value in re.findall(
                    r"([a-z_]+_ms)=([0-9.]+)", line) if key != "window_ms"
            },
        })
    if draw_windows:
        for row in draw_windows:
            row["render_rate_hz"] = row["frames"] * 1000 / row["window_ms"]
        result["draw_profile"] = {
            "windows": len(draw_windows),
            "frames": sum(row["frames"] for row in draw_windows),
            "draws": sum(row["draws"] for row in draw_windows),
            "draws_per_frame_weighted": round(
                sum(row["draws"] for row in draw_windows) / sum(row["frames"] for row in draw_windows), 3),
            "render_rate_hz": describe_rates([row["render_rate_hz"] for row in draw_windows], 30),
            "draws_per_frame_min_window": round(
                min(row["draws"] / row["frames"] for row in draw_windows), 3),
            "draws_per_frame_max_window": round(
                max(row["draws"] / row["frames"] for row in draw_windows), 3),
            "window_ordinal_alignment_note": (
                "Paired with t26 loop windows by ordinal; sampler windows start independently."
            ),
        }
        for stats in target_windows.values():
            paired = [draw_windows[index - 1] for index in stats["window_numbers"]
                      if 0 < index <= len(draw_windows)]
            stats["draw_profile_window_ordinal_match"] = {
                "windows": len(paired),
                "render_rate_hz": describe_rates(
                    [row["render_rate_hz"] for row in paired], 30),
                "draws_per_frame_weighted": round(
                    sum(row["draws"] for row in paired) /
                    sum(row["frames"] for row in paired), 3) if paired else None,
                "mean_cpu_stage_ms_per_window": {
                    key: round(sum(row["cpu_stages_ms"].get(key, 0) for row in paired) /
                               len(paired), 3)
                    for key in sorted({stage for row in paired for stage in row["cpu_stages_ms"]})
                } if paired else {},
                "cpu_stage_us_per_draw_weighted": {
                    key: round(sum(row["cpu_stages_ms"].get(key, 0) for row in paired) *
                               1000 / sum(row["draws"] for row in paired), 3)
                    for key in sorted({stage for row in paired for stage in row["cpu_stages_ms"]})
                } if paired and sum(row["draws"] for row in paired) else {},
                "vc_hashed_kb_per_draw_weighted": round(
                    sum(row["vc_hashed_kb"] for row in paired) /
                    sum(row["draws"] for row in paired), 3)
                    if paired and all(row["vc_hashed_kb"] is not None for row in paired)
                    else None,
                "texture_counts_total": {
                    key: sum(row["texture_counts"].get(key, 0) for row in paired)
                    for key in sorted({key for row in paired for key in row["texture_counts"]})
                },
            }
    fifo_fields = ("fifo_ms", "fifo_cpu_ms", "fifo_cpu_valid", "fifo_calls",
                   "fifo_max_ms", "sleep_ms", "sleep_calls", "sleep_max_ms")
    fifo_windows = list(records(read(folder, "t21-clock.log"), fifo_fields))
    if fifo_windows:
        fifo_wall = sum(row["fifo_ms"] for row in fifo_windows)
        fifo_cpu = sum(row["fifo_cpu_ms"] for row in fifo_windows)
        result["frame_clock_fifo"] = {
            "windows": len(fifo_windows),
            "wall_ms": fifo_wall,
            "cpu_ms": fifo_cpu if all(row["fifo_cpu_valid"] for row in fifo_windows) else None,
            "cpu_over_wall_percent": round(fifo_cpu * 100 / fifo_wall, 2)
                if fifo_wall and all(row["fifo_cpu_valid"] for row in fifo_windows) else None,
            "calls": sum(row["fifo_calls"] for row in fifo_windows),
            "max_call_ms": max(row["fifo_max_ms"] for row in fifo_windows),
            "sleep_wall_ms": sum(row["sleep_ms"] for row in fifo_windows),
            "sleep_calls": sum(row["sleep_calls"] for row in fifo_windows),
            "max_sleep_ms": max(row["sleep_max_ms"] for row in fifo_windows),
        }
    detail_keys = ("present_cpu_ms", "fifo_headers", "fifo_payload",
                   "fifo_3d_methods", "fifo_method_sample_cpu_us",
                   "fifo_method_samples", "fifo_budget_hits",
                   "fifo_draw_methods", "fifo_draw_cpu_us",
                   "ppu_main_cpu_ms", "ppu_main_cpu_valid")
    detail = list(records(read(folder, "t21-clock.log"), detail_keys))
    if detail:
        totals = {key: sum(row[key] for row in detail) for key in detail_keys}
        totals["windows"] = len(detail)
        if not all(row["ppu_main_cpu_valid"] for row in detail):
            totals["ppu_main_cpu_ms"] = None
        count = totals["fifo_method_samples"]
        totals["sampled_nondraw_mean_cpu_us"] = round(
            totals["fifo_method_sample_cpu_us"] / count, 3) if count else None
        count = totals["fifo_draw_methods"]
        totals["draw_mean_cpu_us"] = round(
            totals["fifo_draw_cpu_us"] / count, 3) if count else None
        result["frame_clock_detail"] = totals
    for label, filename, keys in (
        ("spu_outside_wait", "t21-spu-cpu.log", ("task", "cpu_ms", "wall_ms")),
        ("resc_thread", "t21-guest-cpu.log", ("cpu_ms", "wall_ms")),
    ):
        totals = defaultdict(lambda: {"cpu_ms": 0, "wall_ms": 0, "windows": 0})
        for row in records(read(folder, filename), keys):
            value = totals[str(row.get("task", "flip_thread"))]
            value["cpu_ms"] += row["cpu_ms"]
            value["wall_ms"] += row["wall_ms"]
            value["windows"] += 1
        for value in totals.values():
            value["cpu_over_wall_percent"] = round(value["cpu_ms"] * 100 / value["wall_ms"], 2) if value["wall_ms"] else None
        result[label] = dict(totals)
    helper_keys = ("task", "ls_read", "ls_write", "shufb",
                   "ls_read_sample_us", "ls_write_sample_us", "shufb_sample_us",
                   "ls_read_samples", "ls_write_samples", "shufb_samples")
    helper_totals = defaultdict(lambda: defaultdict(int))
    for row in records(read(folder, "t21-spu-cpu.log"), helper_keys):
        value = helper_totals[str(row["task"])]
        value["windows"] += 1
        for key in helper_keys[1:]:
            value[key] += row[key]
    for value in helper_totals.values():
        for kind in ("ls_read", "ls_write", "shufb"):
            samples = value[f"{kind}_samples"]
            value[f"{kind}_sample_mean_cpu_us"] = round(
                value[f"{kind}_sample_us"] / samples, 3) if samples else None
    result["spu_helpers"] = {key: dict(value) for key, value in helper_totals.items()}
    schedule_log = read(folder, "t21-spu-schedule.log")
    schedule = defaultdict(lambda: {
        "windows": 0, "run_wall_ms": 0, "run_cpu_ms": 0,
        "wait_wall_ms": 0, "wait_cpu_ms": 0, "max_wait_ms": 0,
    })
    dispatches = []
    for row in records(schedule_log, ("taskset", "task", "image", "queue_ms")):
        dispatches.append({"taskset": f"0x{row['taskset']:08X}",
                           "task": row["task"], "image": row["image"],
                           "queue_ms": row["queue_ms"]})
    for row in records(schedule_log, ("taskset", "task", "samples", "run_wall_ms",
                                      "run_cpu_ms", "wait_wall_ms", "wait_cpu_ms",
                                      "max_wait_ms")):
        value = schedule[f"0x{row['taskset']:08X}/task{row['task']}"]
        value["windows"] += 1
        for key in ("run_wall_ms", "run_cpu_ms", "wait_wall_ms", "wait_cpu_ms"):
            value[key] += row[key]
        value["max_wait_ms"] = max(value["max_wait_ms"], row["max_wait_ms"])
    for value in schedule.values():
        active = value["run_wall_ms"] + value["wait_wall_ms"]
        value["run_wall_percent"] = round(value["run_wall_ms"] * 100 / active, 2) if active else None
        value["run_cpu_over_wall_percent"] = round(value["run_cpu_ms"] * 100 / value["run_wall_ms"], 2) if value["run_wall_ms"] else None
    result["spu_dispatches"] = dispatches
    result["spu_schedule"] = dict(schedule)
    wake = defaultdict(lambda: {
        "windows": 0, "waited": 0, "latched": 0, "untracked": 0,
        "drained": 0, "wake_sum_us": 0, "wake_max_us": 0,
        "lt1ms": 0, "lt5ms": 0, "lt20ms": 0, "ge20ms": 0,
        "sig_waiting_total": 0, "sig_active_total": 0,
        "sig_coalesced_total": 0,
    })
    wake_keys = ("taskset", "task", "samples", "waited", "latched", "untracked",
                 "drained", "wake_sum_us", "wake_max_us", "lt1ms", "lt5ms",
                 "lt20ms", "ge20ms", "sig_waiting_total", "sig_active_total",
                 "sig_coalesced_total")
    for row in records(read(folder, "t21-spu-wake.log"), wake_keys):
        value = wake[f"0x{row['taskset']:08X}/task{row['task']}"]
        value["windows"] += 1
        for key in ("waited", "latched", "untracked", "drained", "wake_sum_us",
                    "lt1ms", "lt5ms", "lt20ms", "ge20ms"):
            value[key] += row[key]
        for key in ("wake_max_us", "sig_waiting_total", "sig_active_total",
                    "sig_coalesced_total"):
            value[key] = max(value[key], row[key])
    for value in wake.values():
        value["mean_signal_to_resume_us"] = round(value["wake_sum_us"] / value["waited"], 1) if value["waited"] else None
    result["spu_wake"] = dict(wake)
    source = defaultdict(lambda: {
        "spu_waited": 0, "hle_waited": 0,
        "spu_wake_sum_us": 0, "hle_wake_sum_us": 0,
        "spu_wake_max_us": 0, "hle_wake_max_us": 0,
        "line_signals_total": 0,
    })
    source_keys = ("taskset", "task", "spu_waited", "hle_waited",
                   "spu_wake_sum_us", "hle_wake_sum_us", "spu_wake_max_us",
                   "hle_wake_max_us", "line_signals_total")
    for row in records(read(folder, "t21-spu-wake.log"), source_keys):
        value = source[f"0x{row['taskset']:08X}/task{row['task']}"]
        for key in ("spu_waited", "hle_waited", "spu_wake_sum_us", "hle_wake_sum_us"):
            value[key] += row[key]
        for key in ("spu_wake_max_us", "hle_wake_max_us", "line_signals_total"):
            value[key] = max(value[key], row[key])
    for value in source.values():
        for prefix in ("spu", "hle"):
            count = value[f"{prefix}_waited"]
            value[f"{prefix}_mean_signal_to_resume_us"] = round(value[f"{prefix}_wake_sum_us"] / count, 1) if count else None
    result["spu_wake_sources"] = dict(source)
    frames = list(records(read(folder, "t21-frames.log"),
        ("wall_us", "presents", "visual_changes", "held", "failed", "p95_ms_bucket", "max_us")))
    result["frame_windows"] = len(frames)
    if frames:
        result["display"] = {
            "successful_presents": sum(row["presents"] for row in frames),
            "visual_changes_sampled": sum(row["visual_changes"] for row in frames),
            "held": sum(row["held"] for row in frames),
            "failures": sum(row["failed"] for row in frames),
            "worst_window_p95_ms_bucket": max(row["p95_ms_bucket"] for row in frames),
            "p95_bucket_cap_ms": 500,
            "max_interval_ms": max(row["max_us"] for row in frames) / 1000,
        }
    hot_log = read(folder, "t21-spu-hot.log")
    samples = defaultdict(int)
    for row in records(hot_log, ("thread", "image", "pc", "samples")):
        samples[(row["thread"], row["image"], row["pc"])] += row["samples"]
    result["spu_top_function_wall_samples"] = [
        {"thread": key[0], "image": key[1], "pc": f"0x{key[2]:05X}", "samples": count}
        for key, count in sorted(samples.items(), key=lambda item: item[1], reverse=True)[:20]
    ]
    result["profile_dropped_samples"] = sum(row["dropped"] for row in records(hot_log, ("dropped",)))
    result["profile_overflow_threads"] = max((row["overflow_threads"] for row in records(hot_log, ("overflow_threads",))), default=0)
    input_log = read(folder, "t21-input.log")
    result["input_guest_reports_logged"] = sum(line.startswith("guest_read ") for line in input_log.splitlines())
    result["limitations"] = [
        "Presentations may repeat pixels; image hashes cannot prove unique decoded frames or menu arrival.",
        "SPU CPU/wall excludes WAIT_SIGNAL and cannot be read as total console CPU utilization.",
        "SPU schedule windows start after each task's first WAIT_SIGNAL; initial guest work is not included in run totals.",
        "Dispatch queue_ms includes host thread startup; persistent task wake latency remains part of wait_wall_ms.",
        "Signal-to-resume includes host wake scheduling and optional SPU serial lock reacquisition; latched signals are counted separately.",
        "Untracked wakes can come from guest signalled-bit updates outside spu_taskset_signal_task or diagnostic slot exhaustion.",
        "SPU commit timestamps are recorded after the guest atomic write; a rare waiter racing ahead of the commit callback remains untracked.",
        "Function profiles report top10 wall samples per window, not CPU instruction samples; absent hot log means unavailable, not zero cost.",
        "p95 is per display window, quantized to ceil milliseconds; bucket 500 includes longer intervals.",
        "Per-helper and non-draw RSX sampled CPU times include timer overhead and are unsuitable as exact total-cost estimates for very short calls.",
        "The all-costs build adds counters, timer samples, and function stamps; compare its presentation rate with the unprofiled v4 before generalizing.",
    ]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error("Log directory does not exist")
    print(json.dumps(analyze(args.directory), indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
