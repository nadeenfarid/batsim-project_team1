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
- **EASY-EXP**: Experimental version (custom)

## 🛠️ Technologies

- 🧠 **Batsim** – for job simulation
- ⚙️ **C++17** – for algorithm implementation
- 🧱 **Meson + Ninja** – for building the project
- 📦 **batprotocol-cpp**, **intervalset**, **nlohmann_json** – dependencies

## 🚀 How to Build

```bash
meson setup build
ninja -C build
