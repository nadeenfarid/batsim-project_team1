# EASY-Based Scheduling Algorithms with Batsim

This project is a reproduction and extension of experiments from the paper **"Tuning EASY-Backfilling Queues"** by Lelong et al. It implements and evaluates several scheduling algorithms based on the EASY-Backfilling scheme, using the Batsim simulator.

## 📚 Context

Batch scheduling plays a critical role in managing jobs on high-performance computing (HPC) platforms. The EASY-Backfilling strategy is widely used, and this project explores its variants that differ in how they reorder the queue of waiting jobs.

## 🧠 Paper Reference

> **Tuning EASY-Backfilling Queues**
> J. Lelong, V. Reis, D. Trystram
> [Link to the paper](https://hal.science/hal-01522459/document)

## 📌 Implemented Algorithms

Each algorithm is a variation of EASY-Backfilling with a different job reordering policy:

- **EASY-FCFS**: First-Come, First-Served
- **EASY-LCFS**: Last-Come, First-Served
- **EASY-LPF**: Largest Processing First
- **EASY-LQF**: Largest Queue First
- **EASY-SPJ**: Shortest Predicted Job
- **EASY-SQJ**: Smallest Queue Job
- **EASY-EXP**: Expansion Factor Largest First

## 🛠️ Technologies

- 🧠 **Batsim** – for job simulation
- ⚙️ **C++17** – for algorithm implementation
- 🧱 **Meson + Ninja** – for building the project
- 📦 **batprotocol-cpp**, **intervalset**, **nlohmann_json** – dependencies

## 🚀 How to Build

```bash
meson setup build
ninja -C build
```

## ▶️ How to Execute

```bash
python run_easy_variants.py -p assets/20machines.xml -w assets/500jobs.json --build-dir build
```

This command **runs all EASY-based scheduling algorithms** defined in the project on a specific simulation setup using Batsim.

- `-p assets/20machines.xml`: specifies the platform file with 20 machines.
- `-w assets/500jobs.json`: provides the base workload containing 500 jobs.
- `--build-dir build`: tells the script where to find the compiled scheduling plugin (`libeasy_variants.so`).

The script does the following:
1. Runs each scheduling algorithm **10 times** on auto-generated variations of the workload.
2. Runs each algorithm with **two threshold settings**:
   - No threshold (unrestricted backfilling),
   - A 1-hour threshold (restricts job reordering based on wait time).
3. Collects **mean and max waiting times** for each run and saves them in `results.py`.
4. Saves a **timeline plot** of job executions for run #10 of each configuration.
5. Generates **two summary bar charts** in the `out/` directory:
   - `mean_waiting_comparison.png`
   - `max_waiting_comparison.png`

Results are reproducible, self-contained, and give clear visual feedback on the impact of different queue ordering policies and thresholds.