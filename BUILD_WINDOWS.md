# Build on Windows

## Prerequisites

- Qt 6 with modules:
  - `Core`
  - `Widgets`
  - `Pdf`
- CMake (>= 3.21)
- C++ compiler (MSVC via Visual Studio recommended)
- TeX distribution with `pdflatex` in `PATH`:
  - MiKTeX or TeX Live

## Configure and Build

From a Developer Command Prompt (or PowerShell with Qt and compiler env loaded):

```powershell
cmake -S . -B build -G "Ninja"
cmake --build build
```

If you prefer Visual Studio generator:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## Run

With Ninja/single-config:

```powershell
.\build\qtikz.exe
```

With Visual Studio/multi-config:

```powershell
.\build\Release\qtikz.exe
```

## Notes

- Runtime compilation requires `pdflatex` to be reachable in `PATH`.
- If Qt DLLs are not found at runtime, run `windeployqt` on `qtikz.exe`.
