# Zenith-OS

A cross-platform C CLI tool designed to demonstrate OS-level programming by interfacing natively with macOS and Windows APIs. 

It provides simple system instrumentation including process lists, memory usage, disk mounts, alongside a multi-threaded synchronization demo.

## Features

- **Process Management (`--proc`)**: Lists active Process IDs (PIDs), Parent PIDs, and Executable Names.
- **Memory Management (`--mem`)**: Displays Total vs Available Physical RAM using OS-specific routines.
- **File System (`--disk`)**: Enumerates drive letters and mount points, reporting free space capacity.
- **Concurrency Demo (`--sync`)**: Illustrates thread behavior by surfacing a race condition across increments to a global counter, and demonstrates resolving it successfully using mutexes/critical sections.

## Building and Running

### macOS (M1/M2/M3 natively supported)

Compilation utilizes Apple's `clang`.

1. **Compile**:
   ```sh
   make
   ```
2. **Run**:
   ```sh
   ./zenith_os [option]
   ```

### Windows 10/11

#### Option 1: MinGW GCC
1. Ensure `gcc` is populated onto your system `%PATH%`.
2. **Compile**:
   ```cmd
   gcc main.c -o zenith_os.exe
   ```
3. **Run**:
   ```cmd
   .\zenith_os.exe [option]
   ```

#### Option 2: MSVC cl.exe
1. Launch an **x64 Native Tools Command Prompt for VS**.
2. Navigate to this directory.
3. **Compile**:
   ```cmd
   cl /EHsc /Fe:zenith_os.exe main.c
   ```
4. **Run**:
   ```cmd
   .\zenith_os.exe [option]
   ```

## Usage Example

```
> zenith_os --proc
> zenith_os --mem
> zenith_os --disk
> zenith_os --sync
```
