# KTikZ

KTikZ is a KDE/Qt desktop editor for TikZ with live PDF preview and direct manipulation of geometric parameters.

## Overview

- Left pane: LaTeX/TikZ editor powered by `KTextEditor`
- Right pane: compiled PDF preview rendered with `QPdfDocument`
- Bottom pane: compilation log output
- Menu/toolbar actions: `Load`, `Compile`, and `Examples`

## Interactive Editing

The preview supports:

- mouse-wheel zoom
- click-drag pan
- draggable red markers over parsed geometry

Current editable primitives:

- coordinate pairs `(x,y)`
- circles (`radius`)
- rectangles (second corner)
- ellipses (`rx`, `ry`)
- cubic Bezier curves (`control 1`, `control 2`)

Marker changes update the source text and trigger recompilation.

## Grid and Snap

Controls below the preview include:

- snap step combobox: `10 mm`, `5 mm`, `2 mm`, `1 mm`, `0 (free)`
- grid extent spinbox: `20..100 cm`

Behavior:

- non-zero snap values quantize drag operations to the selected step
- `0 (free)` disables snapping but keeps a 10 mm display reference grid
- changing step or extent recompiles automatically

## Examples

The `Examples` menu includes ready-made snippets for:

- line
- polyline
- circle
- rectangle
- ellipse
- Bezier
- mixed playground

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/ktikz
```

## Project Structure

- `src/main.cpp`: application entry point
- `src/mainwindow.h`, `src/mainwindow.cpp`: main UI and application flow
- `src/pdfcanvas.h`, `src/pdfcanvas.cpp`: preview rendering, calibration, markers, drag logic
- `src/compileservice.h`, `src/compileservice.cpp`: TeX generation, grid injection, `pdflatex` execution
- `src/coordinateparser.h`, `src/coordinateparser.cpp`: numeric geometry parsing and formatting
- `src/model.h`: shared geometry data models

## Notes

- Calibration anchors are injected during compilation to align PDF pixels with TikZ coordinates.
- Anchors are drawn on the top layer to remain detectable regardless of figure colors.
