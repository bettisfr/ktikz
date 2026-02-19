# Build on macOS

## Prerequisites

- Qt 6 with modules:
  - `Core`
  - `Widgets`
  - `Pdf`
- CMake (>= 3.21)
- Xcode command line tools (`xcode-select --install`)
- TeX distribution with `pdflatex` in `PATH`:
  - MacTeX or TeX Live

## Configure and Build

```bash
cmake -S . -B build
cmake --build build -j
```

If CMake cannot find Qt, pass the Qt prefix path:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/macos
cmake --build build -j
```

## Run

```bash
./build/qtikz
```

## Notes

- Runtime compilation requires `pdflatex` to be available in shell `PATH`.
- For app packaging/notarization, create a macOS app bundle in a later step.
