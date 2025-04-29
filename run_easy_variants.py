#!/usr/bin/env python3
"""
run_easy_variants.py
--------------------

• Executes each EASY queue twice (no threshold and T = 1 h) on 10 workloads.
• Aggregates mean/max waiting times → results.py
• Saves one timeline per queue per threshold run #10.
• Finally builds two comparison bar-charts:
      out/mean_waiting_comparison.png
      out/max_waiting_comparison.png
"""

from __future__ import annotations
import argparse
import importlib
import importlib.util, json, re, shutil
import subprocess, sys
from importlib.machinery import SourceFileLoader
from datetime import datetime
from pathlib import Path
from typing import List, Tuple

# ------------- CLI -------------------------------------------------------- #
cli = argparse.ArgumentParser()
cli.add_argument("-p","--platform",required=True)
cli.add_argument("-w","--workload",required=True)
cli.add_argument("--build-dir",default="build")
cli.add_argument("--batsim",default=shutil.which("batsim") or "batsim")
cli.add_argument("--results-file",default="results.py")
args = cli.parse_args()

BASE_PLATFORM = Path(args.platform)
BASE_WORKLOAD = Path(args.workload)
BUILD_DIR     = Path(args.build_dir)
PLUGIN        = BUILD_DIR / "libeasy_variants.so"
RESULTS_FILE  = Path(args.results_file)

# ------------- numbers from filenames ------------------------------------ #
def number(stem:str, default:int)->int:
    m=re.search(r"(\d+)",stem); return int(m.group(1)) if m else default
NB_MACHINES = number(BASE_PLATFORM.stem,20)
NB_JOBS     = number(BASE_WORKLOAD.stem,500)

# ------------- load/generate_config -------------------------------------- #
def load_gen():
    try: return importlib.import_module("generate_config")
    except ModuleNotFoundError:
        for d in {BASE_PLATFORM.parent, BASE_WORKLOAD.parent}:
            f=d/"generate_config.py"
            if f.exists():
                l=SourceFileLoader("generate_config",str(f))
                spec=importlib.util.spec_from_loader(l.name,l)
                mod=importlib.util.module_from_spec(spec); l.exec_module(mod) # type: ignore
                sys.modules["generate_config"]=mod; return mod
        raise
gen=load_gen()

# ------------- ensure files ---------------------------------------------- #
def ensure_platform():
    if BASE_PLATFORM.exists(): return
    BASE_PLATFORM.parent.mkdir(parents=True,exist_ok=True)
    gen.generate_platform_xml(NB_MACHINES,BASE_PLATFORM)

def wf_path(i:int)->Path:
    return BASE_WORKLOAD.parent / f"{NB_JOBS}_{i}_jobs.json"
def ensure_workload(i:int)->Path:
    f=wf_path(i)
    if not f.exists():
        f.parent.mkdir(parents=True,exist_ok=True)
        gen.generate_workload_json(NB_JOBS,NB_MACHINES,f)
    return f

# ------------- run batsim ------------------------------------------------- #
EXPORT=re.compile(r"mean_waiting_time=([\d.]+).*max_waiting_time=([\d.]+)")
def run_batsim(arg:str, wf:Path, do_plot:bool)->Tuple[float,float]:
    cmd=[args.batsim,"-l",str(PLUGIN),"0",f"'{arg}'",
         "-p",str(BASE_PLATFORM),"-w",str(wf)]
    out=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT,check=True).stdout
    m=EXPORT.search(out)
    if not m: raise RuntimeError("metrics not found")
    mean,maxw=float(m.group(1)),float(m.group(2))

    if do_plot:
        from evalys.jobset import JobSet
        import matplotlib
        matplotlib.use("Agg"); import matplotlib.pyplot as plt
        js=JobSet.from_csv(Path("out")/"jobs.csv")
        js.plot(with_details=True)
        tag="_T1" if "@1" in arg else ""
        plt.title(f"EASY-{arg}")
        plt.savefig(Path("out")/f"{arg.split('@')[0]}{tag}.png",
                    dpi=150,bbox_inches="tight"); plt.close()
    return mean,maxw

# ------------- results helpers ------------------------------------------- #
def load_results(p:Path)->List[dict]:
    if not p.exists(): return []
    spec=importlib.util.spec_from_file_location("res",p)
    mod=importlib.util.module_from_spec(spec); spec.loader.exec_module(mod) # type: ignore
    return list(getattr(mod,"RESULTS",[]))
def save_results(p:Path,data:List[dict]):     # prettified JSON list
    with open(p,"w") as f:
        f.write("# auto-generated\nRESULTS=[\n")
        for r in data: f.write(f"    {json.dumps(r)},\n")
        f.write("]\n")

# ------------- summary plots --------------------------------------------- #
def make_summary_plots(data:List[dict]):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    queues=["exp","fcfs","lcfs","lpf","lqf","spf","sqf"]
    mean_noT=[]; mean_T=[]; max_noT=[]; max_T=[]
    for q in queues:
        noT=next(r for r in data if r["queue"]==q and r["threshold_h"] == -1)
        T1=next(r for r in data if r["queue"]==q and r["threshold_h"]==1)
        mean_noT.append(noT["mean_waiting_time"])
        mean_T.append(T1["mean_waiting_time"])
        max_noT.append(noT["max_waiting_time"])
        max_T.append(T1["max_waiting_time"])

    def barplot(values1, values2, title, fname, ylabel):
        x=np.arange(len(queues)); w=0.35
        fig,ax=plt.subplots(figsize=(10,6))
        ax.bar(x-w/2,values1,w,label="no threshold")
        ax.bar(x+w/2,values2,w,label="T = 1 h")
        ax.set_xticks(x); ax.set_xticklabels([q.upper() for q in queues])
        ax.set_ylabel(ylabel); ax.set_title(title); ax.legend()
        plt.tight_layout(); plt.savefig(Path("out")/fname,dpi=150); plt.close()

    barplot(mean_noT,mean_T,"Average waiting time vs. queue",
            "mean_waiting_comparison.png","mean waiting time (s)")
    barplot(max_noT,max_T,"Max waiting time vs. queue",
            "max_waiting_comparison.png","max waiting time (s)")
    print("✓ summary plots saved in out/")

# ------------- main ------------------------------------------------------- #
def main():
    ensure_platform()
    res=load_results(RESULTS_FILE)
    QUEUES=["exp","fcfs","lcfs","lpf","lqf","spf","sqf"]

    for q in QUEUES:
        for th in (-1,1):
            arg=q if th == -1 else f"{q}@{th}"
            tag=f"{q}{'' if th == -1 else '_T1'}"
            means=maxs=[]
            print(f"\n== {tag.upper()} ==")
            for i in range(1,11):
                wf=ensure_workload(i)
                mean,maxw=run_batsim(arg,wf,do_plot=(i==10))
                means.append(mean); maxs.append(maxw)
                print(f"  run {i}/10  mean={mean:.2f} max={maxw:.0f}")
            res.append({
                "queue": q,"threshold_h": th,
                "machines": NB_MACHINES,"jobs": NB_JOBS,
                "mean_waiting_time": round(sum(means)/10,6),
                "max_waiting_time": round(sum(maxs)/10,6),
                "timestamp": datetime.now().isoformat(timespec="seconds")
            })

    save_results(RESULTS_FILE,res)
    make_summary_plots(res)
    print(f"\n✓ metrics + plots done — see results.py and out/")

if __name__=="__main__":
    main()
