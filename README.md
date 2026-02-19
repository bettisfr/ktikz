# KTikZ

KDE/Qt TikZ editor prototype with live compile preview and draggable coordinate markers.

## Current Behavior

- Left pane: Kate-like LaTeX editor (`KTextEditor`)
- Right pane: compiled PDF preview (`QPdfDocument` rendering)
- Bottom pane: live LaTeX compilation output
- Toolbar/menu actions: `Load`, `Compile`, `Quit`

Preview features:

- Mouse wheel zoom
- Click-drag pan
- Red `+` markers rendered for detected `(x,y)` coordinates
- Marker drag updates source coordinates and triggers auto-recompile

Grid/snap control (below right pane):

- Allowed values: `10 mm`, `5 mm`, `2 mm`, `1 mm`, `0 (free)`
- Non-zero values: grid is injected into compiled TikZ + marker drag snapping enabled
- `0 (free)`: free marker drag, but preview grid defaults to 10 mm reference lines
- Changing this value auto-recompiles

## Coordinate Detection

Coordinates are extracted from source using numeric pairs in parentheses:

- `(x,y)`
- `(x, y)`

with integer/float/scientific formats.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/ktikz
```

## Source Layout

- `src/main.cpp`: app entrypoint
- `src/mainwindow.{h,cpp}`: UI composition and signal wiring
- `src/pdfcanvas.{h,cpp}`: PDF preview, calibration, marker draw/drag
- `src/compileservice.{h,cpp}`: TeX generation, grid injection, pdflatex process
- `src/coordinateparser.{h,cpp}`: coordinate extraction/formatting
- `src/model.h`: shared coordinate structs
