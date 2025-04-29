#!/usr/bin/env python3
"""
run_easy_variants.py
--------------------

• Accepts the SAME arguments as the original script:
      -p / --platform  <platform_xml>
      -w / --workload  <jobs_json>  (for the *pattern* only)

• Runs each easy_{q}_{q}.so plug-in 10×  on
      <jobs>_<i>_jobs.json   (i = 1..10)
  creating missing files with generate_config.py if needed.

• Aggregates mean/max waiting times (mean of means) and stores one
  record per queue in results.py.

• Generates ONE Evalys timeline plot per queue, on the 10-th run,
  saved to  out/<queue>.png .
"""

from __future__ import annotations
import argparse
import importlib
import json
import random
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import List

##############################################################################
# --------------------------- command-line ---------------------------------- #
##############################################################################
cli = argparse.ArgumentParser()
cli.add_argument("-p", "--platform", required=True,
                 help="base platform file (e.g. assets/10machines.xml)")
cli.add_argument("-w", "--workload", required=True,
                 help="base workload file (e.g. assets/30jobs.json)")
cli.add_argument("--build-dir", default="build",
                 help="directory containing libeasy_*.so (default: build)")
cli.add_argument("--batsim", default=shutil.which("batsim") or "batsim",
                 help="path to batsim executable")
cli.add_argument("--results-file", default="results.py",
                 help="aggregated output (python list)")
args = cli.parse_args()

BASE_PLATFORM = Path(args.platform)
BASE_WORKLOAD = Path(args.workload)
BUILD_DIR     = Path(args.build_dir)
RESULTS_FILE  = Path(args.results_file)

def load_generate_config():
    import importlib, importlib.util
    from importlib.machinery import SourceFileLoader

    try:
        return importlib.import_module("generate_config")
    except ModuleNotFoundError:
        for folder in {Path(args.platform).parent, Path(args.workload).parent}:
            cand = folder / "generate_config.py"
            if cand.exists():
                loader = SourceFileLoader("generate_config", str(cand))
                spec   = importlib.util.spec_from_loader(loader.name, loader)
                module = importlib.util.module_from_spec(spec)
                loader.exec_module(module)          # type: ignore
                sys.modules["generate_config"] = module
                return module
        raise  # still not found

generate_config = load_generate_config()

##############################################################################
# --------------------- extract numbers from file names --------------------- #
##############################################################################
def int_from_stem(stem: str) -> int | None:
    m = re.search(r"(\d+)", stem)
    return int(m.group(1)) if m else None

nb_machines = int_from_stem(BASE_PLATFORM.stem) or 20
nb_jobs     = int_from_stem(BASE_WORKLOAD.stem) or 500

##############################################################################
# -------------------- import the realistic generator ---------------------- #
##############################################################################
try:
    generate_config = importlib.import_module("generate_config")
except ModuleNotFoundError:
    sys.exit("❌  Could not import generate_config.py - put it on PYTHONPATH")

##############################################################################
# ----------------------- helper: ensure platform --------------------------- #
##############################################################################
def ensure_platform() -> None:
    if BASE_PLATFORM.exists():
        return
    BASE_PLATFORM.parent.mkdir(parents=True, exist_ok=True)
    generate_config.generate_platform_xml(nb_machines, BASE_PLATFORM)
    print(f"✓ generated platform {BASE_PLATFORM}")

##############################################################################
# ------------------- helper: ensure workload i (1..10) --------------------- #
##############################################################################
def workload_path(i: int) -> Path:
    stem  = f"{nb_jobs}_{i}_jobs"
    return BASE_WORKLOAD.parent / f"{stem}.json"

def ensure_workload(i: int) -> Path:
    wf = workload_path(i)
    if wf.exists():
        return wf
    wf.parent.mkdir(parents=True, exist_ok=True)
    generate_config.generate_workload_json(nb_jobs, nb_machines, wf)
    return wf

##############################################################################
# ------------------- helper: run batsim once & parse ---------------------- #
##############################################################################
EXPORT_RE = re.compile(
    r"mean_waiting_time=(?P<mean>[\d.]+).*max_waiting_time=(?P<max>[\d.]+)"
)

def run_batsim(queue: str, wf: Path, plot: bool) -> tuple[float, float]:
    plugin = BUILD_DIR / f"libeasy_{queue}_{queue}.so"
    if not plugin.exists():
        raise FileNotFoundError(plugin)

    cmd = [
        args.batsim,
        "-l", str(plugin), "0", "''",
        "-p", str(BASE_PLATFORM),
        "-w", str(wf),
    ]
    out = subprocess.run(cmd, text=True,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT,
                         check=True).stdout
    m = EXPORT_RE.search(out)
    if not m:
        raise RuntimeError("metrics line not found in batsim output")
    mean_wait, max_wait = float(m.group("mean")), float(m.group("max"))

    if plot:
        from evalys.jobset import JobSet
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        csv = Path("out") / "jobs.csv"
        js  = JobSet.from_csv(csv)
        js.plot(with_details=True)
        plt.title(f"EASY-{queue.upper()}-{queue.upper()}  ({wf.name})")
        out_png = Path("out") / f"{queue}.png"
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"    ↳ timeline saved to {out_png}")
    return mean_wait, max_wait

##############################################################################
# ------------------- load / save aggregated results ----------------------- #
##############################################################################
def load_results(path: Path) -> List[dict]:
    if not path.exists():
        return []
    spec = importlib.util.spec_from_file_location("results", path)
    mod  = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)          # type: ignore
    return list(getattr(mod, "RESULTS", []))

def save_results(path: Path, data: List[dict]):
    with open(path, "w") as f:
        f.write("# Auto-generated – averaged over 10 workload files\nRESULTS = [\n")
        for d in data:
            f.write(f"    {json.dumps(d)},\n")
        f.write("]\n")

##############################################################################
# ----------------------------- main loop ---------------------------------- #
##############################################################################
def main() -> None:
    ensure_platform()
    aggregated = load_results(RESULTS_FILE)

    for queue in ["exp", "fcfs", "lcfs", "lpf", "lqf", "spf", "sqf"]:
        means, maxs = [], []
        print(f"\n=== {queue.upper()} ===")
        for i in range(1, 11):
            wf = ensure_workload(i)
            print(f"  · run {i}/10 on {wf.name} …", end="", flush=True)
            mean, mx = run_batsim(queue, wf, plot=(i == 10))
            means.append(mean)
            maxs.append(mx)
            print(f" done  (mean={mean:.3f}, max={mx:.3f})")
        avg_mean = sum(means) / 10.0
        avg_max  = sum(maxs)  / 10.0
        print(f"→ aggregated  mean={avg_mean:.3f}  max={avg_max:.3f}")

        aggregated.append(
            {
                "queue": queue,
                "machines": nb_machines,
                "jobs_per_workload": nb_jobs,
                "runs": 10,
                "mean_waiting_time": round(avg_mean, 6),
                "max_waiting_time":  round(avg_max, 6),
                "timestamp": datetime.now().isoformat(timespec="seconds"),
            }
        )

    save_results(RESULTS_FILE, aggregated)
    print(f"\n✓ updated {RESULTS_FILE}")

if __name__ == "__main__":
    from datetime import datetime
    main()
