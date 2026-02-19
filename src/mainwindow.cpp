#include "mainwindow.h"

#include <QAction>
#include <QComboBox>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "compileservice.h"
#include "coordinateparser.h"
#include "pdfcanvas.h"
#include "appconfig.h"

namespace {
class latexhighlighter : public QSyntaxHighlighter {
public:
    explicit latexhighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {
        QTextCharFormat command_fmt;
        command_fmt.setForeground(QColor("#1d4ed8"));
        command_fmt.setFontWeight(QFont::Bold);
        rules_.push_back({QRegularExpression(R"(\\[A-Za-z@]+)"), command_fmt});

        QTextCharFormat env_fmt;
        env_fmt.setForeground(QColor("#7c3aed"));
        rules_.push_back({QRegularExpression(R"(\\(begin|end)\{[^}]+\})"), env_fmt});

        QTextCharFormat number_fmt;
        number_fmt.setForeground(QColor("#059669"));
        rules_.push_back({QRegularExpression(R"([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)"), number_fmt});

        QTextCharFormat comment_fmt;
        comment_fmt.setForeground(QColor("#6b7280"));
        rules_.push_back({QRegularExpression(R"(%[^\n]*)"), comment_fmt});
    }

protected:
    void highlightBlock(const QString &text) override {
        for (const rule &r : rules_) {
            QRegularExpressionMatchIterator it = r.pattern.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), r.format);
            }
        }
    }

private:
    struct rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    std::vector<rule> rules_;
};

QString wrap_tikz_document(const QString &tikz_body) {
    return "\\documentclass[tikz,border=10pt]{standalone}\n"
           "\\usepackage{tikz}\n"
           "\\begin{document}\n"
           "\\begin{tikzpicture}\n" + tikz_body +
           "\\end{tikzpicture}\n"
           "\\end{document}\n";
}

void append_colored_log(QTextEdit *output, const QString &text, const QColor &color) {
    if (!output) {
        return;
    }
    QTextCursor cursor(output->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(color);
    cursor.insertText(text, fmt);
    if (!text.endsWith('\n')) {
        cursor.insertText("\n", fmt);
    }
    output->setTextCursor(cursor);
    output->ensureCursorVisible();
}
} // namespace

mainwindow::mainwindow(QWidget *parent) : QMainWindow(parent) {
    update_window_title();
    resize(1200, 800);

    QFont editor_font;
    editor_font.setFamily(QStringLiteral("Monospace"));
    editor_font.setStyleHint(QFont::Monospace);
    editor_font.setPointSize(12);
    editor_ = new QPlainTextEdit(this);
    editor_->setFont(editor_font);
    editor_->setTabStopDistance(editor_->fontMetrics().horizontalAdvance(QStringLiteral(" ")) * 4);
    editor_->setPlainText(QString());
    editor_->document()->setModified(false);
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &mainwindow::on_document_modified_changed);
    new latexhighlighter(editor_->document());
    update_window_title();

    preview_canvas_ = new pdfcanvas(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(editor_);

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

    output_ = new QTextEdit(this);
    output_->setReadOnly(true);
    output_->setPlaceholderText("Compilation output will appear here...");
    auto *output_section = new QWidget(this);
    auto *output_layout = new QVBoxLayout(output_section);
    output_layout->setContentsMargins(6, 4, 6, 6);
    output_layout->setSpacing(4);
    auto *output_header = new QWidget(output_section);
    auto *output_header_layout = new QHBoxLayout(output_header);
    output_header_layout->setContentsMargins(0, 0, 0, 0);
    output_header_layout->setSpacing(6);
    auto *output_label = new QLabel("Console output", output_header);
    auto *clear_log_btn = new QPushButton("Clear", output_header);
    connect(clear_log_btn, &QPushButton::clicked, this, [this]() {
        if (output_) {
            output_->clear();
        }
    });
    output_header_layout->addWidget(output_label);
    output_header_layout->addStretch(1);
    output_header_layout->addWidget(clear_log_btn);
    output_layout->addWidget(output_header);
    output_layout->addWidget(output_, 1);

    auto *main_splitter = new QSplitter(Qt::Vertical, this);
    main_splitter->addWidget(splitter);
    main_splitter->addWidget(output_section);
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

void mainwindow::closeEvent(QCloseEvent *event) {
    const QString app_name = QString::fromLatin1(appconfig::APP_NAME);
    const QMessageBox::StandardButton res = QMessageBox::question(
        this,
        "Quit " + app_name,
        "Do you want to quit " + app_name + "?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (res == QMessageBox::Yes) {
        event->accept();
    } else {
        event->ignore();
    }
}

void mainwindow::update_window_title() {
    const QString app_name = QString::fromLatin1(appconfig::APP_NAME);
    const QString file_part =
        current_file_path_.isEmpty() ? QStringLiteral("untitled") : QFileInfo(current_file_path_).fileName();
    const bool modified = editor_ && editor_->document()->isModified();
    setWindowTitle(app_name + " - " + file_part + (modified ? "*" : ""));
}

void mainwindow::create_menu_and_toolbar() {
    auto *file_menu = menuBar()->addMenu("File");
    auto *edit_menu = menuBar()->addMenu("Edit");
    auto *build_menu = menuBar()->addMenu("Build");
    auto *examples_menu = menuBar()->addMenu("Examples");
    auto *help_menu = menuBar()->addMenu("Help");

    auto *new_act = new QAction(QIcon::fromTheme("document-new"), "New", this);
    auto *load_act = new QAction(QIcon::fromTheme("document-open"), "Load", this);
    auto *save_act = new QAction(QIcon::fromTheme("document-save"), "Save", this);
    auto *save_as_act = new QAction(QIcon::fromTheme("document-save-as"), "Save As...", this);
    auto *undo_act = new QAction(QIcon::fromTheme("edit-undo"), "Undo", this);
    auto *redo_act = new QAction(QIcon::fromTheme("edit-redo"), "Redo", this);
    auto *indent_act = new QAction(QIcon::fromTheme("format-indent-more"), "Indent", this);
    auto *compile_act = new QAction(QIcon::fromTheme("system-run"), "Compile", this);
    auto *quit_act = new QAction(QIcon::fromTheme("application-exit"), "Quit", this);
    const QString app_name = QString::fromLatin1(appconfig::APP_NAME);
    auto *about_ktikz_act = new QAction(QIcon::fromTheme("help-about"), "About " + app_name, this);

    load_act->setIconVisibleInMenu(true);
    compile_act->setIconVisibleInMenu(true);
    quit_act->setIconVisibleInMenu(true);

    new_act->setShortcut(QKeySequence::New);
    load_act->setShortcut(QKeySequence("Ctrl+O"));
    save_act->setShortcut(QKeySequence::Save);
    save_as_act->setShortcut(QKeySequence("Ctrl+Shift+S"));
    undo_act->setShortcut(QKeySequence::Undo);
    redo_act->setShortcut(QKeySequence::Redo);
    indent_act->setShortcut(QKeySequence("Ctrl+Shift+I"));
    compile_act->setShortcut(QKeySequence("F5"));
    quit_act->setShortcut(QKeySequence::Quit);

    connect(new_act, &QAction::triggered, this, &mainwindow::new_file);
    connect(load_act, &QAction::triggered, this, &mainwindow::load_file);
    connect(save_act, &QAction::triggered, this, &mainwindow::save_file);
    connect(save_as_act, &QAction::triggered, this, &mainwindow::save_file_as);
    connect(undo_act, &QAction::triggered, this, [this]() {
        if (editor_) {
            editor_->undo();
        }
    });
    connect(redo_act, &QAction::triggered, this, [this]() {
        if (editor_) {
            editor_->redo();
        }
    });
    connect(indent_act, &QAction::triggered, this, &mainwindow::indent_latex);
    connect(compile_act, &QAction::triggered, this, &mainwindow::compile);
    connect(quit_act, &QAction::triggered, this, &QWidget::close);
    connect(about_ktikz_act, &QAction::triggered, this, [this]() {
        const QString app_name = QString::fromLatin1(appconfig::APP_NAME);
        QMessageBox::about(
            this,
            "About " + app_name,
            app_name + "\n\n"
            "A KDE/Qt editor for TikZ with live PDF preview and interactive shape editing.\n\n"
            "Francesco Betti Sorbelli <francesco.bettisorbelli@unipg.it>");
    });

    auto add_example = [this, examples_menu](const QString &label, const QString &body) {
        auto *act = new QAction(label, this);
        connect(act, &QAction::triggered, this, [this, body, label]() {
            if (!editor_) {
                return;
            }
            editor_->setPlainText(wrap_tikz_document(body));
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
    add_example(
        "Bezier",
        "  \\draw[blue,very thick] (0,0) .. controls (1.5,2.0) and (3.0,-1.0) .. (4.0,1.0)\n"
        "                       .. controls (5.2,2.3) and (6.8,0.2) .. (8.0,1.6);\n");
    add_example(
        "Mixed Playground",
        "  \\draw[->,thick] (-9,0) -- (9,0);\n"
        "  \\draw[->,thick] (0,-9) -- (0,9);\n"
        "  \\draw[blue,dashed,ultra thick,->] (1,1) -- (2,3) -- (4,1) -- (6,3)"
        "                                -- (7.2,2.2) -- (8.0,4.0) -- (8.8,3.1);\n"
        "  \\draw[brown, thick, fill=yellow, fill opacity=0.6] (-4,-3) circle (2);\n"
        "  \\draw[magenta, ultra thick, fill=green!20] (6,5) ellipse (2 and 1);\n"
        "  \\draw[orange,thick,fill=red,fill opacity=0.4] (2,-4) rectangle (6,-1);\n"
        "  \\draw[red,thick] (-4,4) .. controls (-6,6) and (-1,4) .. (-3,7)"
        "                   .. controls (-2,8.2) and (0.5,6.3) .. (1.2,7.1);\n"
        "  \\node at (2,6) {" + app_name + " ... enjoy};\n");

    file_menu->addAction(new_act);
    file_menu->addAction(load_act);
    file_menu->addAction(save_act);
    file_menu->addAction(save_as_act);
    file_menu->addSeparator();
    file_menu->addAction(quit_act);
    edit_menu->addAction(undo_act);
    edit_menu->addAction(redo_act);
    build_menu->addAction(compile_act);
    build_menu->addAction(indent_act);
    help_menu->addAction(about_ktikz_act);

    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(new_act);
    toolbar->addAction(load_act);
    toolbar->addSeparator();
    toolbar->addAction(save_act);
    toolbar->addAction(save_as_act);
    toolbar->addSeparator();
    toolbar->addAction(undo_act);
    toolbar->addAction(redo_act);
    toolbar->addSeparator();
    toolbar->addAction(indent_act);
    toolbar->addAction(compile_act);
}

void mainwindow::new_file() {
    if (!editor_) {
        return;
    }

    if (editor_->document()->isModified()) {
        const QMessageBox::StandardButton res = QMessageBox::question(
            this,
            "New file",
            "Discard unsaved changes and create a new file?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (res != QMessageBox::Yes) {
            return;
        }
    }

    editor_->setPlainText(QString());
    current_file_path_.clear();
    editor_->document()->setModified(false);
    update_window_title();
    statusBar()->showMessage("New file", 1500);
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
    editor_->setPlainText(in.readAll());
    editor_->document()->setModified(false);
    current_file_path_ = path;
    update_window_title();
    statusBar()->showMessage("Loaded " + QFileInfo(path).fileName(), 3000);
}

void mainwindow::save_file() {
    if (!editor_) {
        return;
    }

    QString path = current_file_path_;
    if (path.isEmpty()) {
        save_file_as();
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        statusBar()->showMessage("Save failed", 3000);
        return;
    }

    QTextStream out(&file);
    out << editor_->toPlainText();
    file.close();

    current_file_path_ = path;
    editor_->document()->setModified(false);
    update_window_title();
    statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 3000);
}

void mainwindow::save_file_as() {
    if (!editor_) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        "Save As",
        current_file_path_.isEmpty() ? QDir::homePath() : current_file_path_,
        "TeX files (*.tex *.tikz);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        statusBar()->showMessage("Save failed", 3000);
        return;
    }

    QTextStream out(&file);
    out << editor_->toPlainText();
    file.close();

    current_file_path_ = path;
    editor_->document()->setModified(false);
    update_window_title();
    statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 3000);
}

void mainwindow::on_document_modified_changed(bool) {
    update_window_title();
}

void mainwindow::compile() {
    if (!editor_ || !compile_service_) {
        return;
    }

    const QString source_text = editor_->toPlainText();
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

void mainwindow::indent_latex() {
    if (!editor_) {
        return;
    }

    const QString text = editor_->toPlainText();
    const QStringList lines = text.split('\n', Qt::KeepEmptyParts);

    auto count_occurrences = [](const QString &line, const QString &token) {
        int count = 0;
        int pos = 0;
        while ((pos = line.indexOf(token, pos)) >= 0) {
            ++count;
            pos += token.size();
        }
        return count;
    };

    QStringList out_lines;
    out_lines.reserve(lines.size());
    int indent_level = 0;

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();

        int pre_dedent = 0;
        if (trimmed.startsWith("\\end{")) {
            pre_dedent = 1;
        }
        indent_level = qMax(0, indent_level - pre_dedent);

        if (trimmed.isEmpty()) {
            out_lines.push_back(QString());
        } else {
            out_lines.push_back(QString(indent_level * 2, ' ') + trimmed);
        }

        const int begins = count_occurrences(trimmed, "\\begin{");
        const int ends = count_occurrences(trimmed, "\\end{");
        indent_level = qMax(0, indent_level + begins - ends);
    }

    editor_->setPlainText(out_lines.join('\n'));
    statusBar()->showMessage("LaTeX indentation applied", 1500);
}

void mainwindow::on_compile_service_output(const QString &text) {
    append_colored_log(output_, text, QColor("#1f2937"));
}

void mainwindow::on_compile_finished(bool success, const QString &pdf_path, const QString &) {
    if (!success) {
        append_colored_log(output_, "[Status] Compiled with errors", QColor("#dc2626"));
        statusBar()->showMessage("Compile failed", 3000);
        return;
    }

    if (!preview_canvas_->load_pdf(pdf_path)) {
        on_compile_service_output("[Preview] Failed to load generated PDF");
        append_colored_log(output_, "[Status] Compiled with errors", QColor("#dc2626"));
        statusBar()->showMessage("Preview load failed", 3000);
        return;
    }

    append_colored_log(output_, "[Status] Compiled successfully", QColor("#16a34a"));
    statusBar()->showMessage("Compile successful", 2500);
}

void mainwindow::on_coordinate_dragged(int index, double x, double y) {
    if (!editor_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(coordinate_refs_.size())) {
        return;
    }

    const coord_ref ref = coordinate_refs_[index];
    QString text = editor_->toPlainText();
    if (ref.end <= ref.start || ref.end > text.size()) {
        return;
    }

    const QString replacement = "(" + coordinateparser::format_number(x) + "," + coordinateparser::format_number(y) + ")";
    text.replace(ref.start, ref.end - ref.start, replacement);
    editor_->setPlainText(text);
    compile();
}

void mainwindow::on_circle_radius_dragged(int index, double radius) {
    if (!editor_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(circle_refs_.size())) {
        return;
    }

    const circle_ref ref = circle_refs_[index];
    QString text = editor_->toPlainText();
    if (ref.radius_end <= ref.radius_start || ref.radius_end > text.size()) {
        return;
    }

    const QString replacement = coordinateparser::format_number(radius);
    text.replace(ref.radius_start, ref.radius_end - ref.radius_start, replacement);
    editor_->setPlainText(text);
    compile();
}

void mainwindow::on_ellipse_radii_dragged(int index, double rx, double ry) {
    if (!editor_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(ellipse_refs_.size())) {
        return;
    }

    const ellipse_ref ref = ellipse_refs_[index];
    QString text = editor_->toPlainText();
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

    editor_->setPlainText(text);
    compile();
}

void mainwindow::on_bezier_control_dragged(int index, int control_idx, double x, double y) {
    if (!editor_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(bezier_refs_.size())) {
        return;
    }

    const bezier_ref ref = bezier_refs_[index];
    QString text = editor_->toPlainText();
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
    editor_->setPlainText(text);
    compile();
}

void mainwindow::on_rectangle_corner_dragged(int index, double x2, double y2) {
    if (!editor_ || !compile_service_) {
        return;
    }
    if (compile_service_->is_busy()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(rectangle_refs_.size())) {
        return;
    }

    const rectangle_ref ref = rectangle_refs_[index];
    QString text = editor_->toPlainText();
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

    editor_->setPlainText(text);
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
