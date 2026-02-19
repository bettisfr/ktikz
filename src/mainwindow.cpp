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
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "compileservice.h"
#include "coordinateparser.h"
#include "pdfcanvas.h"

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

    editor_doc_->setText(QString());

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

    controls_layout->addWidget(step_label);
    controls_layout->addWidget(grid_step_combo_);
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
    connect(grid_step_combo_, &QComboBox::currentIndexChanged, this, &mainwindow::on_grid_step_changed);

    connect(compile_service_, &compileservice::output_text, this, &mainwindow::on_compile_service_output);
    connect(compile_service_, &compileservice::compile_finished, this, &mainwindow::on_compile_finished);

    preview_canvas_->set_snap_mm(grid_snap_mm_);

    create_menu_and_toolbar();
    statusBar()->showMessage("Ready");
}

void mainwindow::create_menu_and_toolbar() {
    auto *file_menu = menuBar()->addMenu("File");
    auto *build_menu = menuBar()->addMenu("Build");

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
    preview_canvas_->set_coordinates(coordinateparser::extract_pairs(source_text));
    compile_service_->compile(source_text, grid_display_mm_);
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
