# KTikZ

A KDE/Qt TikZ editor prototype with live preview, smart coordinate markers, and drag-to-edit workflow.

## ✨ Features

- 📝 **Left pane**: Kate-like LaTeX editor (`KTextEditor`)
- 🖼️ **Right pane**: compiled PDF preview (`QPdfDocument` rendering)
- 📜 **Bottom pane**: live LaTeX compilation output
- 🧭 **Toolbar/Menu**: `Load`, `Compile`, `Quit`

### Preview interactions

- 🔍 Mouse wheel zoom
- ✋ Click-drag pan
- ➕ Red `+` markers for detected `(x,y)` coordinates
- 🧲 Marker drag updates source coordinates and auto-recompiles

## 📐 Grid & Snap Control

The control below the right pane supports:

- `10 mm`
- `5 mm`
- `2 mm`
- `1 mm`
- `0 (free)`

Behavior:

- Non-zero values: grid is injected into compiled TikZ and marker drag snaps to that step.
- `0 (free)`: marker drag is free-hand, while preview grid defaults to **10 mm** major references.
- Changing the value triggers automatic recompile.

## 🧠 Coordinate Detection

Coordinates are parsed from source in numeric pair form:

- `(x,y)`
- `(x, y)`

Supported numbers: integer, decimal, scientific notation.

## 🏗️ Build

```bash
cmake -S . -B build
cmake --build build -j
```

## ▶️ Run

```bash
./build/ktikz
```

## 🗂️ Project Structure

- `src/main.cpp` - app entrypoint
- `src/mainwindow.h`, `src/mainwindow.cpp` - UI composition and signal wiring
- `src/pdfcanvas.h`, `src/pdfcanvas.cpp` - PDF preview, calibration, marker draw/drag
- `src/compileservice.h`, `src/compileservice.cpp` - TeX generation, grid injection, `pdflatex` execution
- `src/coordinateparser.h`, `src/coordinateparser.cpp` - coordinate extraction and numeric formatting
- `src/model.h` - shared coordinate structs

## ⚠️ Notes

- Calibration anchors are injected at compile-time to align preview pixels and TikZ coordinates reliably.
- Anchors are placed on the top layer so user drawings do not hide them.
