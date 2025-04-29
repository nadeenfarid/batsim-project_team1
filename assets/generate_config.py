#!/usr/bin/env python3
"""
generate_config.py – realistic platform + workload generator for Batsim.

Usage
-----
$ python3 generate_config.py -n 40 -j 500
"""

import argparse
import json
import math
import random
from pathlib import Path
from xml.dom.minidom import Document

# --------------------------------------------------------------------------- #
# 1. Tunable constants (feel free to edit)
# --------------------------------------------------------------------------- #
DEFAULT_MACHINES = 20
DEFAULT_JOBS     = 200

# machine-speed classes (Gf)
SPEED_CLASSES = {
    "slow":   (5.0,  8.0),
    "medium": (8.0, 16.0),
    "fast":   (16.0, 26.0),
}

# runtime distribution (seconds) – log-normal parameters
RUNTIME_MU, RUNTIME_SIGMA = 5.3, 1.0       # mean≈200 s, heavy tail
OVERESTIMATE_MIN, OVERESTIMATE_MAX = 1.2, 4.0   # walltime / runtime

# size (nb_nodes) – log-normal then clipped to [1, nb_machines]
SIZE_MU, SIZE_SIGMA = 0.8, 1.0              # mostly small jobs, rare giants

# correlation exponent: runtime ∝ (size)**corr_exp
CORR_EXP = 0.4

# Poisson arrival: mean inter-arrival time (seconds)
MEAN_IAT = 15.0
# --------------------------------------------------------------------------- #

def lognormal_int(mu, sigma, lo, hi):
    """Helper: bounded int from log-normal."""
    while True:
        val = int(round(random.lognormvariate(mu, sigma)))
        if lo <= val <= hi:
            return val

# --------------------------------------------------------------------------- #
# 2. Platform XML
# --------------------------------------------------------------------------- #
def generate_platform_xml(nb_machines: int, out_path: Path):
    """
    Build SimGrid platform without the extra blank line at the top that
    was produced by minidom in the original script.
    """
    doc = Document()

    # <!DOCTYPE …>
    doctype = doc.implementation.createDocumentType(
        qualifiedName="platform",
        publicId=None,
        systemId="https://simgrid.org/simgrid.dtd",
    )
    doc.appendChild(doctype)

    # NOTE: we do *not* inject our own <?xml?> PI; minidom's toprettyxml()
    # will write it correctly, without preceding newline/space.
    platform = doc.createElement("platform")
    platform.setAttribute("version", "4.1")
    doc.appendChild(platform)

    zone = doc.createElement("zone")
    zone.setAttribute("id", "multiple_machines")
    zone.setAttribute("routing", "Full")
    platform.appendChild(zone)

    # randomise speed class composition
    classes = list(SPEED_CLASSES.items())
    for i in range(nb_machines):
        cls, (lo, hi) = random.choice(classes)
        speed = random.uniform(lo, hi)
        host = doc.createElement("host")
        host.setAttribute("id", f"node_{i}")
        host.setAttribute("speed", f"{speed:.2f}Gf")
        zone.appendChild(host)

    master = doc.createElement("host")
    master.setAttribute("id", "master_host")
    master.setAttribute("speed", "100Mf")
    zone.appendChild(master)

    # Write file – strip the first blank line inserted by toprettyxml
    xml_str = doc.toprettyxml(indent="  ", encoding="utf-8").decode()
    xml_str = xml_str.lstrip()                # remove any leading whitespace
    out_path.write_text(xml_str)
    print(f"✓ Platform XML written to {out_path}")

# --------------------------------------------------------------------------- #
# 3. Workload JSON
# --------------------------------------------------------------------------- #
def generate_workload_json(nb_jobs: int, nb_machines: int, out_path: Path):
    jobs   = []
    profiles = {}
    now    = 0.0

    for jid in range(1, nb_jobs + 1):
        # --- arrival time (Poisson) ---
        iat   = random.expovariate(1.0 / MEAN_IAT)
        now  += iat
        subtime = int(now)

        # --- size / runtime correlation ---
        size  = lognormal_int(SIZE_MU, SIZE_SIGMA, 1, nb_machines)
        # encourage larger sizes to have longer runtime
        runtime = lognormal_int(RUNTIME_MU, RUNTIME_SIGMA, 1, 10**6)
        runtime = int(runtime * (size ** CORR_EXP))

        # --- wall-time (user guess) ---
        factor   = random.uniform(OVERESTIMATE_MIN, OVERESTIMATE_MAX)
        walltime = max(1, int(math.ceil(runtime * factor)))

        profile = f"P{jid}"
        jobs.append(
            {
                "id": str(jid),
                "profile": profile,
                "res": size,
                "walltime": walltime,
                "subtime": subtime,
            }
        )
        profiles[profile] = {"delay": runtime, "type": "delay"}

    data = {
        "description": f"{nb_jobs} jobs – synthetic heavy-tail",
        "nb_res": nb_machines,
        "jobs": jobs,
        "profiles": profiles,
    }
    out_path.write_text(json.dumps(data, indent=2))
    print(f"✓ Workload JSON written to {out_path}")

# --------------------------------------------------------------------------- #
# 4. CLI
# --------------------------------------------------------------------------- #
def main():
    parser = argparse.ArgumentParser(description="Generate realistic Batsim test data")
    parser.add_argument("-n", "--machines", type=int, default=DEFAULT_MACHINES)
    parser.add_argument("-j", "--jobs",     type=int, default=DEFAULT_JOBS)
    args = parser.parse_args()

    plat_file = Path(f"{args.machines}machines.xml")
    job_file  = Path(f"{args.jobs}jobs.json")

    random.seed()            # system entropy

    generate_platform_xml(args.machines, plat_file)
    generate_workload_json(args.jobs, args.machines, job_file)

if __name__ == "__main__":
    main()
