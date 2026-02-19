#include "mainwindow.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "compileservice.h"
#include "coordinateparser.h"
#include "pdfcanvas.h"

namespace {
QString wrap_tikz_document(const QString &tikz_body) {
    return "\\documentclass[tikz,border=10pt]{standalone}\n"
           "\\usepackage{tikz}\n"
           "\\begin{document}\n"
           "\\begin{tikzpicture}\n" + tikz_body +
           "\\end{tikzpicture}\n"
           "\\end{document}\n";
}
} // namespace

mainwindow::mainwindow(QWidget *parent) : KMainWindow(parent) {
    setWindowTitle("KTikZ");
    resize(1200, 800);

    auto *editor_backend = KTextEditor::Editor::instance();
    if (!editor_backend) {
        statusBar()->showMessage("KTextEditor backend unavailable");
        return;
    }

    editor_doc_ = editor_backend->createDocument(this);
    editor_view_ = editor_doc_->createView(this);
    editor_doc_->setMode(QStringLiteral("LaTeX"));

    QFont editor_font;
    editor_font.setFamily(QStringLiteral("Monospace"));
    editor_font.setStyleHint(QFont::Monospace);
    editor_font.setPointSize(12);
    editor_view_->setConfigValue(QStringLiteral("font"), editor_font);

    editor_doc_->setText(wrap_tikz_document(
        "  \\draw[blue,thick] (0,0) .. controls (1.5,2.0) and (3.0,-1.0) .. (4.0,1.0);\n"));

    preview_canvas_ = new pdfcanvas(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(editor_view_);

    auto *right_pane = new QWidget(this);
    auto *right_layout = new QVBoxLayout(right_pane);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(6);
    right_layout->addWidget(preview_canvas_, 1);

    auto *controls_row = new QWidget(right_pane);
    auto *controls_layout = new QHBoxLayout(controls_row);
    controls_layout->setContentsMargins(6, 0, 6, 6);
    controls_layout->setSpacing(8);

    auto *step_label = new QLabel("Grid/Snap:", controls_row);
    grid_step_combo_ = new QComboBox(controls_row);
    grid_step_combo_->addItem("10 mm", 10);
    grid_step_combo_->addItem("5 mm", 5);
    grid_step_combo_->addItem("2 mm", 2);
    grid_step_combo_->addItem("1 mm", 1);
    grid_step_combo_->addItem("0 (free)", 0);
    grid_step_combo_->setCurrentIndex(0);
    auto *extent_label = new QLabel("Extent:", controls_row);
    grid_extent_spin_ = new QSpinBox(controls_row);
    grid_extent_spin_->setRange(20, 100);
    grid_extent_spin_->setSingleStep(5);
    grid_extent_spin_->setSuffix(" cm");
    grid_extent_spin_->setValue(grid_extent_cm_);

    controls_layout->addWidget(step_label);
    controls_layout->addWidget(grid_step_combo_);
    controls_layout->addWidget(extent_label);
    controls_layout->addWidget(grid_extent_spin_);
    controls_layout->addStretch(1);

    right_layout->addWidget(controls_row, 0);
    splitter->addWidget(right_pane);
    splitter->setSizes({600, 600});

    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    output_->setPlaceholderText("Compilation output will appear here...");

    auto *main_splitter = new QSplitter(Qt::Vertical, this);
    main_splitter->addWidget(splitter);
    main_splitter->addWidget(output_);
    main_splitter->setSizes({650, 150});

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(main_splitter);
    setCentralWidget(central);

    compile_service_ = new compileservice(this);

    connect(preview_canvas_, &pdfcanvas::coordinate_dragged, this, &mainwindow::on_coordinate_dragged);
    connect(preview_canvas_, &pdfcanvas::circle_radius_dragged, this, &mainwindow::on_circle_radius_dragged);
    connect(preview_canvas_, &pdfcanvas::ellipse_radii_dragged, this, &mainwindow::on_ellipse_radii_dragged);
    connect(preview_canvas_, &pdfcanvas::bezier_control_dragged, this, &mainwindow::on_bezier_control_dragged);
    connect(preview_canvas_, &pdfcanvas::rectangle_corner_dragged, this, &mainwindow::on_rectangle_corner_dragged);
    connect(grid_step_combo_, &QComboBox::currentIndexChanged, this, &mainwindow::on_grid_step_changed);
    connect(grid_extent_spin_, &QSpinBox::valueChanged, this, &mainwindow::on_grid_extent_changed);

    connect(compile_service_, &compileservice::output_text, this, &mainwindow::on_compile_service_output);
    connect(compile_service_, &compileservice::compile_finished, this, &mainwindow::on_compile_finished);

    preview_canvas_->set_snap_mm(grid_snap_mm_);

    create_menu_and_toolbar();
    statusBar()->showMessage("Ready");
}

void mainwindow::create_menu_and_toolbar() {
    auto *file_menu = menuBar()->addMenu("File");
    auto *build_menu = menuBar()->addMenu("Build");
    auto *examples_menu = menuBar()->addMenu("Examples");

    auto *load_act = new QAction(QIcon::fromTheme("document-open"), "Load", this);
    auto *compile_act = new QAction(QIcon::fromTheme("system-run"), "Compile", this);
    auto *quit_act = new QAction(QIcon::fromTheme("application-exit"), "Quit", this);

    load_act->setIconVisibleInMenu(true);
    compile_act->setIconVisibleInMenu(true);
    quit_act->setIconVisibleInMenu(true);

    load_act->setShortcut(QKeySequence("Ctrl+O"));
    compile_act->setShortcut(QKeySequence("F5"));
    quit_act->setShortcut(QKeySequence::Quit);

    connect(load_act, &QAction::triggered, this, &mainwindow::load_file);
    connect(compile_act, &QAction::triggered, this, &mainwindow::compile);
    connect(quit_act, &QAction::triggered, this, &QWidget::close);

    auto add_example = [this, examples_menu](const QString &label, const QString &body) {
        auto *act = new QAction(label, this);
        connect(act, &QAction::triggered, this, [this, body, label]() {
            if (!editor_doc_) {
                return;
            }
            editor_doc_->setText(wrap_tikz_document(body));
            statusBar()->showMessage("Loaded example: " + label, 1500);
            if (compile_service_ && !compile_service_->is_busy()) {
                compile();
            }
        });
        examples_menu->addAction(act);
    };

    add_example("Line", "  \\draw[thick] (0,0) -- (4,2);\n");
    add_example("Polyline", "  \\draw[blue,dashed,thick,->] (0,0) -- (1,1.5) -- (2.2,0.3) -- (3.8,1.8);\n");
    add_example("Circle", "  \\draw[thick] (0,0) circle (1.5);\n");
    add_example("Rectangle", "  \\draw[thick] (-1.5,-1) rectangle (2,1.2);\n");
    add_example("Ellipse", "  \\draw[thick] (0,0) ellipse (2 and 1);\n");
    add_example("Bezier", "  \\draw[blue,thick] (0,0) .. controls (1.5,2.0) and (3.0,-1.0) .. (4.0,1.0);\n");
    add_example(
        "Mixed Playground",
        "  \\draw[->,thick] (-1.5,0) -- (10.5,0);\n"
        "  \\draw[->,thick] (0,-1.5) -- (0,6.0);\n"
        "  \\draw[blue,dashed,thick,->] (0.5,0.4) -- (2.0,2.2) -- (4.2,0.9) -- (6.8,2.8);\n"
        "  \\draw[thick] (2.4,1.1) circle (0.9);\n"
        "  \\draw[thick] (6.6,2.7) ellipse (1.8 and 0.8);\n"
        "  \\draw[thick] (7.4,-0.8) rectangle (9.8,1.4);\n"
        "  \\draw[red,thick] (1.0,4.2) .. controls (3.2,5.4) and (5.9,2.2) .. (8.8,4.8);\n"
        "  \\node at (9.3,5.4) {KTikZ};\n");

    file_menu->addAction(load_act);
    file_menu->addSeparator();
    file_menu->addAction(quit_act);
    build_menu->addAction(compile_act);

    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(load_act);
    toolbar->addAction(compile_act);
}

void mainwindow::load_file() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Load TikZ/LaTeX File",
        QDir::homePath(),
        "TeX files (*.tex *.tikz);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        statusBar()->showMessage("Load failed", 3000);
        return;
    }

    QTextStream in(&file);
    editor_doc_->setText(in.readAll());
    current_file_path_ = path;
    statusBar()->showMessage("Loaded " + QFileInfo(path).fileName(), 3000);
}

void mainwindow::compile() {
    if (!editor_doc_ || !compile_service_) {
        return;
    }

    const QString source_text = editor_doc_->text();
    coordinate_refs_ = coordinateparser::extract_refs(source_text);
    circle_refs_ = coordinateparser::extract_circle_refs(source_text);
    ellipse_refs_ = coordinateparser::extract_ellipse_refs(source_text);
    bezier_refs_ = coordinateparser::extract_bezier_refs(source_text);
    rectangle_refs_ = coordinateparser::extract_rectangle_refs(source_text);
    preview_canvas_->set_coordinates(coordinateparser::extract_pairs(source_text));
    preview_canvas_->set_circles(coordinateparser::extract_circle_pairs(source_text));
    preview_canvas_->set_ellipses(coordinateparser::extract_ellipse_pairs(source_text));
    preview_canvas_->set_beziers(coordinateparser::extract_bezier_pairs(source_text));
    preview_canvas_->set_rectangles(coordinateparser::extract_rectangle_pairs(source_text));
    compile_service_->compile(source_text, grid_display_mm_, grid_extent_cm_);
    statusBar()->showMessage("Compiling...");
}

void mainwindow::on_compile_service_output(const QString &text) {
    if (!output_) {
        return;
    }
    output_->moveCursor(QTextCursor::End);
    output_->insertPlainText(text);
    if (!text.endsWith('\n')) {
        output_->insertPlainText("\n");
    }
    output_->moveCursor(QTextCursor::End);
}

void mainwindow::on_compile_finished(bool success, const QString &pdf_path, const QString &) {
    if (!success) {
        statusBar()->showMessage("Compile failed", 3000);
        return;
    }

    if (!preview_canvas_->load_pdf(pdf_path)) {
        on_compile_service_output("[Preview] Failed to load generated PDF");
        statusBar()->showMessage("Preview load failed", 3000);
        return;
    }

    statusBar()->showMessage("Compile successful", 2500);
}

void mainwindow::on_coordinate_dragged(int index, double x, double y) {
    if (!editor_doc_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(coordinate_refs_.size())) {
        return;
    }

    const coord_ref ref = coordinate_refs_[index];
    QString text = editor_doc_->text();
    if (ref.end <= ref.start || ref.end > text.size()) {
        return;
    }

    const QString replacement = "(" + coordinateparser::format_number(x) + "," + coordinateparser::format_number(y) + ")";
    text.replace(ref.start, ref.end - ref.start, replacement);
    editor_doc_->setText(text);
    compile();
}

void mainwindow::on_circle_radius_dragged(int index, double radius) {
    if (!editor_doc_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(circle_refs_.size())) {
        return;
    }

    const circle_ref ref = circle_refs_[index];
    QString text = editor_doc_->text();
    if (ref.radius_end <= ref.radius_start || ref.radius_end > text.size()) {
        return;
    }

    const QString replacement = coordinateparser::format_number(radius);
    text.replace(ref.radius_start, ref.radius_end - ref.radius_start, replacement);
    editor_doc_->setText(text);
    compile();
}

void mainwindow::on_ellipse_radii_dragged(int index, double rx, double ry) {
    if (!editor_doc_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(ellipse_refs_.size())) {
        return;
    }

    const ellipse_ref ref = ellipse_refs_[index];
    QString text = editor_doc_->text();
    if (ref.rx_end <= ref.rx_start || ref.ry_end <= ref.ry_start) {
        return;
    }
    if (ref.rx_end > text.size() || ref.ry_end > text.size()) {
        return;
    }

    const QString rx_str = coordinateparser::format_number(rx);
    const QString ry_str = coordinateparser::format_number(ry);

    // Replace from right to left so offsets remain valid.
    if (ref.rx_start > ref.ry_start) {
        text.replace(ref.rx_start, ref.rx_end - ref.rx_start, rx_str);
        text.replace(ref.ry_start, ref.ry_end - ref.ry_start, ry_str);
    } else {
        text.replace(ref.ry_start, ref.ry_end - ref.ry_start, ry_str);
        text.replace(ref.rx_start, ref.rx_end - ref.rx_start, rx_str);
    }

    editor_doc_->setText(text);
    compile();
}

void mainwindow::on_bezier_control_dragged(int index, int control_idx, double x, double y) {
    if (!editor_doc_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(bezier_refs_.size())) {
        return;
    }

    const bezier_ref ref = bezier_refs_[index];
    QString text = editor_doc_->text();
    int x_start = 0;
    int x_end = 0;
    int y_start = 0;
    int y_end = 0;
    if (control_idx == 1) {
        x_start = ref.x1_start;
        x_end = ref.x1_end;
        y_start = ref.y1_start;
        y_end = ref.y1_end;
    } else if (control_idx == 2) {
        x_start = ref.x2_start;
        x_end = ref.x2_end;
        y_start = ref.y2_start;
        y_end = ref.y2_end;
    } else {
        return;
    }
    if (x_end <= x_start || y_end <= y_start || x_end > text.size() || y_end > text.size()) {
        return;
    }

    const QString x_str = coordinateparser::format_number(x);
    const QString y_str = coordinateparser::format_number(y);
    if (x_start > y_start) {
        text.replace(x_start, x_end - x_start, x_str);
        text.replace(y_start, y_end - y_start, y_str);
    } else {
        text.replace(y_start, y_end - y_start, y_str);
        text.replace(x_start, x_end - x_start, x_str);
    }
    editor_doc_->setText(text);
    compile();
}

void mainwindow::on_rectangle_corner_dragged(int index, double x2, double y2) {
    if (!editor_doc_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(rectangle_refs_.size())) {
        return;
    }

    const rectangle_ref ref = rectangle_refs_[index];
    QString text = editor_doc_->text();
    if (ref.x2_end <= ref.x2_start || ref.y2_end <= ref.y2_start) {
        return;
    }
    if (ref.x2_end > text.size() || ref.y2_end > text.size()) {
        return;
    }

    const QString x2_str = coordinateparser::format_number(x2);
    const QString y2_str = coordinateparser::format_number(y2);

    // Replace from right to left so offsets remain valid.
    if (ref.x2_start > ref.y2_start) {
        text.replace(ref.x2_start, ref.x2_end - ref.x2_start, x2_str);
        text.replace(ref.y2_start, ref.y2_end - ref.y2_start, y2_str);
    } else {
        text.replace(ref.y2_start, ref.y2_end - ref.y2_start, y2_str);
        text.replace(ref.x2_start, ref.x2_end - ref.x2_start, x2_str);
    }

    editor_doc_->setText(text);
    compile();
}

void mainwindow::on_grid_step_changed(int) {
    const int selected = grid_step_combo_ ? grid_step_combo_->currentData().toInt() : 10;
    grid_snap_mm_ = qMax(0, selected);
    grid_display_mm_ = (grid_snap_mm_ == 0) ? 10 : grid_snap_mm_;
    preview_canvas_->set_snap_mm(grid_snap_mm_);

    statusBar()->showMessage(
        grid_snap_mm_ == 0
            ? "Grid: 10 mm, Snap: free hand"
            : ("Grid/Snap step: " + QString::number(grid_snap_mm_) + " mm"),
        1500);

    if (compile_service_ && !compile_service_->is_busy()) {
        compile();
    }
}

void mainwindow::on_grid_extent_changed(int value) {
    grid_extent_cm_ = qBound(20, value, 100);
    statusBar()->showMessage("Grid extent: " + QString::number(grid_extent_cm_) + " cm", 1500);
    if (compile_service_ && !compile_service_->is_busy()) {
        compile();
    }
}
