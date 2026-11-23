# CPU Load Balancing Simulator

A pure-C Operating Systems simulator demonstrating dynamic CPU load balancing across multiple processor cores. It includes both a terminal-based simulation (`main.c`) and an interactive Windows GUI simulator (`gui_simulator.c`) built using the Win32 API.

## Project Files
- `gui_simulator.c` — Interactive desktop GUI built with Windows GDI (`windows.h`) supporting dynamic task generation and scheduling algorithms.
- `main.c` — Terminal-based CPU load balancing simulation.

## How to Compile & Run (Windows / GCC)

### 1. GUI Simulator
```bash
gcc gui_simulator.c -o gui_simulator.exe -lgdi32
./gui_simulator.exe