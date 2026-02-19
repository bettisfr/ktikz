# ktikz

Reset baseline for the new app design.

Current behavior:

- KDE-native main window (`KMainWindow`)
- Menu + toolbar actions with KDE theme icons
- Central canvas shows only a coordinate grid at startup

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/ktikz
```
