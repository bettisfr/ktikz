# ktikz

Minimal TikZ editor prototype with split view:

- Left: LaTeX/TikZ source editor
- Right: preview (first PDF page rendered to PNG)
- Bottom: live LaTeX compiler output
- Top menu/toolbar actions: `Load` and `Compile`

## Requirements

- Qt5 or Qt6 Widgets development packages
- `pdflatex` in `PATH`
- `pdftoppm` in `PATH` (from `poppler-utils`) for preview conversion

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/ktikz
```

## Notes

- `Compile` runs `pdflatex` on a temporary file.
- On success it runs `pdftoppm` and refreshes the right pane preview.
