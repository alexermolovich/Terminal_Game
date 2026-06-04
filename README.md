# Traffic Signal Light Simulator

A small C++17 terminal simulator for a configurable traffic light cycle. It renders a live dashboard with the active signal phase, remaining time, a progress bar, a simple road scene, and controls for advancing simulated time.

## Features

- Interactive terminal dashboard with ANSI color output.
- Configurable phase durations and signal states through an INI file.
- Road scene showing the vehicle, stop line, intersection, and active light.
- Time controls for advancing by 1, 10, 100, or a custom number of seconds.
- Input validation for malformed config files and unsafe duration values.

## Requirements

- A C++17 compiler, such as `g++`.
- `make` for the included Makefile.
- A terminal that supports ANSI escape sequences.

On Windows, `make` may not be available in PowerShell by default. The project builds cleanly from WSL, Git Bash, MSYS2, MinGW, or any environment that provides `make` and `g++`.

## Quick Start

Build the simulator:

```sh
make
```

Run it:

```sh
./traffic_signal
```

Or build and run in one step:

```sh
make run
```

If you are using WSL from Windows, open a WSL shell in this project directory, then run the same `make` and `./traffic_signal` commands.

## Controls

| Key | Action |
| --- | --- |
| Left arrow | Move to the previous advance button |
| Right arrow | Move to the next advance button |
| Enter or Space | Advance simulated time by the selected amount |
| `q` | Quit |

The first three buttons advance by `+1s`, `+10s`, and `+100s`. The last button lets you enter a custom number of seconds.

## Configuration

The simulator currently loads the default config from:

```text
configs/one_way.ini
```

Example:

```ini
# Default one-direction traffic light sequence.
[controller]
name = One Direction Demo
layout = one-direction
phase_durations_ms = 15000, 4000, 12000, 5000

[signal]
label = Main Lane
states = RED, YELLOW, GREEN, RED
```

`phase_durations_ms` and `states` must contain the same number of entries. Durations are positive milliseconds, and each state name is displayed in the dashboard. Built-in color rendering is provided for `RED`, `YELLOW`, and `GREEN`.

## Project Layout

```text
.
|-- configs/
|   `-- one_way.ini
|-- src/
|   `-- main.cpp
|-- Makefile
`-- README.md
```

## Cleanup

Remove generated build output:

```sh
make clean
```

## Verified

This project was built and launched successfully from the project root with:

```sh
make
printf q | ./traffic_signal
```
