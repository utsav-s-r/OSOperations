# Zenith-OS

A cross-platform C CLI tool designed to demonstrate OS-level programming by interfacing natively with macOS and Windows APIs. 

It provides simple system instrumentation including process lists, memory usage, disk mounts, alongside a multi-threaded synchronization demo.

## Features

- **Process Management (`--proc`)**: Lists active Process IDs (PIDs), Parent PIDs, and Executable Names.
- **Memory Management (`--mem`)**: Displays Total vs Available Physical RAM using OS-specific routines.
- **File System (`--disk`)**: Enumerates drive letters and mount points, reporting free space capacity.
- **Concurrency Demo (`--sync`)**: Illustrates thread behavior by surfacing a race condition across increments to a global counter, and demonstrates resolving it successfully using mutexes/critical sections.

## Building and Running

Zenith-OS uses CMake for cross-platform compilation. 

### Prerequisites
- CMake (3.10 or higher)
- A C compiler (Clang/GCC for macOS/Linux, MSVC or MinGW for Windows)
- `ncurses` library (automatically located by CMake on Unix systems)

### macOS / Linux

1. **Generate build files and compile**:
   ```sh
   mkdir build && cd build
   cmake ..
   cmake --build .
   ```
2. **Run**:
   ```sh
   ./zenith_os [option]
   ```

### Windows 10/11

1. **Generate build files and compile** (using a Developer Command Prompt or an environment with CMake):
   ```cmd
   mkdir build
   cd build
   cmake ..
   cmake --build .
   ```
2. **Run**:
   ```cmd
   .\Debug\zenith_os.exe [option]
   ```
   *(Note: The exact path may vary based on your generator. Single-configuration generators like Ninja or MinGW Makefiles will place the executable at `.\zenith_os.exe`.)*

## Usage Example

```
> zenith_os --proc
> zenith_os --mem
> zenith_os --disk
> zenith_os --sync
```
