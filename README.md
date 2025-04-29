
# Session 1: Getting started
First, clone this repository somewhere on your filesystem.

```sh
git clone https://gitlab.com/mpoquet-courses/sched-with-batsim.git
```

**Unless otherwise stated, all provided commands should be run from the root directory of the cloned repository.**

## Software environment
This section describes how to get into an environment where all the software is available for you to use.
Two methods to get into such an environment are listed below, in order of preference.

You can use the following commands to check if all the required softwares are available or not.

```sh
batsim --version
meson --version
ninja --version
pkg-config --version
gdb --version
cgdb --version
```

### Method 1: Nix
Nix a generic purpose package manager that tries as hard as possible to make builds reproducible.
Nix is strongly recommended for any scientific usage, since it not only produces reproducible binaries, but also allows good traceability of an experiment.

Steps to get into a Batsim environment.

1. Install Nix. Up-to-date information is there: https://nixos.org/download/  
  - If you have no installation privileges, try `nix-portable` instead: https://github.com/DavHau/nix-portable
2. Enable Nix flakes. This is done by modifying a configuration file (or creating it first if needed).
   Up-to-date information is there, probably on the "Other Distros, without Home-Manager" section: https://nixos.wiki/wiki/Flakes
3. Optional but recommended: Run this command to use [our binary cache](https://app.cachix.org/cache/capack) instead of compiling software on your local machine:
   `nix develop --extra-substituters 'https://capack.cachix.org' --extra-trusted-public-keys 'capack.cachix.org-1:38D+QFk3JXvMYJuhSaZ+3Nm/Qh+bZJdCrdu4pkIh5BU='`
4. Run this command to enter a new shell from which you should be able to run all softwares: `nix develop`


### Method 2: Build it yourself
Feeling extra adventurous? You can build everything yourself. This is not really documented but here some guidelines.

- You should be able to get these from the package manager of any decent Linux distro: a C++ compilation toolchain, [Meson](https://mesonbuild.com/), [Ninja](https://ninja-build.org/), [pkg-config](https://en.wikipedia.org/wiki/Pkg-config), [Boost](https://en.wikipedia.org/wiki/Boost_(C%2B%2B_libraries)) and [nlohmann_json](https://github.com/nlohmann/json).
- Either get SimGrid from your Linux distro package manager if available, or build it from [source](https://framagit.org/simgrid/simgrid) then install it.
  SimGrid is usually built using [CMake](https://en.wikipedia.org/wiki/CMake).
  As I write these lines, Batsim should work with SimGrid 3.36.0.
- Build and install [intervalset](https://framagit.org/batsim/intervalset) from source.
- Build and install [batprotocol-cpp](https://framagit.org/batsim/batprotocol) from source.
- Build and install [batsim](https://framagit.org/batsim/batsim) from source. Use the `batprotocol` branch. Batsim may require more dependencies, install these when Meson yells at you because it cannot find them.

## Run your first simulation
Yay, most programs and libraries should now be available!

A simple scheduling code is provided in the repository (`exec1by1.cpp`), as well as a description on how to build it (`meson.build`).
Running the following commands shoud compile the algorithm into a shared library (`.so` [ELF](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format) file).

```sh
# setup (generate and configure) a build directory named 'build'
meson setup build

# call ninja, which is in charge of executing the compilation workflow generated my meson
ninja -C buid
```

Your build directory should now contain a `.so` file for the `exec1by1` scheduling algorithm.  
You can list them by running `ls -l build/*.so`

You can now run Batsim, which usually requires three main inputs.
- A SimGrid platform file, which describes the resources used in the simulation.
  More information in [SimGrid documentation](https://simgrid.org/doc/latest/Platform.html)
- A Batsim workload file, which describes what user requests are done at which time (_jobs_), as well as how to simulate each job (_profiles_).
  More information in [Batsim documentation](https://batsim.readthedocs.io/en/latest/input-workload.html)
- A Batsim _external decision component (edc)_.
  You have just built one, as _EDC_ is just Batsim jargon to name a scheduler.

The following command should run Batsim with the scheduler you just built, a tiny platform of 1 machine, and a tiny workload of 2 jobs.

```sh
batsim -l ./build/libexec1by1.so 0 '' -p assets/1machine.xml -w assets/2jobs.json
```

## A glimpse at the simulation result
By default, Batsim writes output files into an `out` directory in your current directory.
Where to write output files can be customized with Batsim's `-e` command-line option.  
You can run Batsim with the `--help` option for details, and to list the available options.

Batsim should have generated two output files.
- `schedule.csv` contains various metrics about the resulting schedule of the simulation.
  This file is similar to what Batsim prints at the end of the simulation.
- `jobs.csv` contains various information about the jobs of the simulation.
  There should be one line per job.
  Some fields identify the job (`job_id` and `workload_name`).
  Some fields conveniently provide you user-given information on the job request (`requested_number_of_resources`, `requested_time`).
  Some fields give you information about the job execution (`starting_time`, `finish_time`, `allocated_resources`).

Have a look at the `jobs.csv` file.
Using a tool that can nicely print CSV files is recommended, such as [bat](https://github.com/sharkdp/bat) for terminal use, [R](https://en.wikipedia.org/wiki/R_(programming_language)) or [Python](https://en.wikipedia.org/wiki/Python_(programming_language)) for data analysis use, or alternatively any interactive spreadsheet software.


## Run your first simulation in a debugger
The provided setup should enable you to smootly run the simulation from a debugger.
This is very convenient to debug your code, or just to see what it does step by step.

The debugger should find the sources of your scheduler easily, as you compiled it yourself.
This is another story for the software ecosystem that calls you code, or for the code that your scheduler calls.
The `${GDB_DIR_ARGS}` environment contains the additional arguments that should be given to `gdb` so that it finds the sources of `batsim`, `batprotocol-cpp` and `intervalset`.

You can use `gdb` directly if you want, or any tool that decorates it with some user interface.
Here is an example using `cgdb`, which is a terminal user interface on top of `gdb`, and which should be available in the provided environment.

```sh
cgdb ${GDB_DIR_ARGS} --args batsim -l ./build/libexec1by1.so 0 '' -p assets/1machine.xml -w assets/2jobs.json
```

Before running the program (by calling the `run` command), you can configure on which events the debugger should stop.
This is typically with breakpoints, conditional breakpoints or watchpoints.
- `break batsim_edc_take_decisions` will make the debugger stop whenever your code is called to decide something.

## Analyze and visualize simulation results
In most real scenarios, you need to compute some analysis on Batsim outputs.
Here you don't... but you'll soon need to do it, so let us do something simple first 😜.

Create a program in your favorite data analysis programming language (R, Julia, Python...) to do the following.
- Read the `jobs.csv` file output from Batsim.
- Recompute the waiting time of each job from its `starting_time` and its `finish_time`.
- Compute aggregation of the waiting time of all the jobs (minimum, median, arithmetic mean, maximum), and print it.

Create a program in your favorite data visualization programming language (R, Julia, gnuplot, Python...) to do the following.
- Read the `jobs.csv` file output from Batsim.
- Plot the Gantt chart of the jobs.
  - Here is an old example in R + tidyverse: https://adfaure.github.io/blog/gantt-charts/
  - Here are many examples in Python: https://github.com/oar-team/evalys/tree/master


# Session 2: Write your own scheduler
Implement FCFS into a new Batsim scheduler with the following steps.

1. Copy `src/libexec1by1.cpp` into `src/fcfs.cpp`.
2. Modify `meson.build` to also compile a `fcfs` shared library, using `src/fcfs.cpp` instead of `src/libexec1by1.cpp`.
3. Run `ninja -C build` and you should see your new scheduler.
4. Modify `src/fcfs.cpp` so it runs a FCFS algorithm that can run jobs in parallel.

Similarly, implement EDF into a new Batsim scheduler with similar steps as for FCFS.

To test that your algorithms work as expected, create a SimGrid platform that contains several hosts, create a workload with at least 5 jobs, and create a workload with jobs that requests several hosts.

# Session 3: Backfilling (N machines)
TODO

# Acknowledgment
This tutorial is heavily inspired from Mael Madon's work on https://gitlab.irit.fr/sepia-pub/mael/RM4ES-practicals.
