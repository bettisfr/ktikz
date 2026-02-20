#include "mainwindow.h"

#include <algorithm>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QSyntaxHighlighter>
#include <QTimer>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSettings>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QStyleFactory>

#include "compileservice.h"
#include "coordinateparser.h"
#include "pdfcanvas.h"
#include "appconfig.h"
#include "settingsdialog.h"

namespace {
class linenumberedit;

class linenumberarea : public QWidget {
public:
    explicit linenumberarea(QWidget *parent, linenumberedit *editor) : QWidget(parent), editor_(editor) {}
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    linenumberedit *editor_;
};

class linenumberedit : public QPlainTextEdit {
public:
    explicit linenumberedit(QWidget *parent = nullptr) : QPlainTextEdit(parent) {
        line_number_area_ = new linenumberarea(this, this);
        connect(this, &QPlainTextEdit::blockCountChanged, this, &linenumberedit::update_line_number_area_width);
        connect(this, &QPlainTextEdit::updateRequest, this, &linenumberedit::update_line_number_area);
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() { line_number_area_->update(); });
        update_line_number_area_width(0);
    }

    int line_number_area_width() const {
        if (!show_line_numbers_) {
            return 0;
        }
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void line_number_area_paint_event(QPaintEvent *event) {
        if (!show_line_numbers_) {
            return;
        }
        QPainter painter(line_number_area_);
        painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));

        QTextBlock block = firstVisibleBlock();
        int block_number = block.blockNumber();
        int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + static_cast<int>(blockBoundingRect(block).height());

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(block_number + 1);
                const bool current = (textCursor().blockNumber() == block_number);
                painter.setPen(current ? palette().color(QPalette::Text) : palette().color(QPalette::Mid));
                painter.drawText(0,
                                 top,
                                 line_number_area_->width() - 4,
                                 fontMetrics().height(),
                                 Qt::AlignRight,
                                 number);
            }

            block = block.next();
            top = bottom;
            bottom = top + static_cast<int>(blockBoundingRect(block).height());
            ++block_number;
        }
    }

    void set_line_numbers_visible(bool visible) {
        show_line_numbers_ = visible;
        line_number_area_->setVisible(show_line_numbers_);
        update_line_number_area_width(0);
        viewport()->update();
    }

    bool line_numbers_visible() const {
        return show_line_numbers_;
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QPlainTextEdit::resizeEvent(event);
        const QRect cr = contentsRect();
        line_number_area_->setGeometry(QRect(cr.left(), cr.top(), line_number_area_width(), cr.height()));
    }

private:
    void update_line_number_area_width(int) {
        setViewportMargins(line_number_area_width(), 0, 0, 0);
    }

    void update_line_number_area(const QRect &rect, int dy) {
        if (dy) {
            line_number_area_->scroll(0, dy);
        } else {
            line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());
        }

        if (rect.contains(viewport()->rect())) {
            update_line_number_area_width(0);
        }
    }

    linenumberarea *line_number_area_ = nullptr;
    bool show_line_numbers_ = true;
};

QSize linenumberarea::sizeHint() const {
    return QSize(editor_->line_number_area_width(), 0);
}

void linenumberarea::paintEvent(QPaintEvent *event) {
    editor_->line_number_area_paint_event(event);
}

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

QPalette make_dark_palette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(45, 45, 45));
    palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
    palette.setColor(QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    palette.setColor(QPalette::Text, QColor(230, 230, 230));
    palette.setColor(QPalette::Button, QColor(53, 53, 53));
    palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    return palette;
}
} // namespace

mainwindow::mainwindow(QWidget *parent) : QMainWindow(parent) {
    update_window_title();
    resize(1200, 800);

    QFont editor_font;
    editor_font.setFamily(editor_font_family_);
    editor_font.setStyleHint(QFont::Monospace);
    editor_font.setPointSize(editor_font_size_);
    editor_ = new linenumberedit(this);
    editor_->setFont(editor_font);
    editor_->setTabStopDistance(editor_->fontMetrics().horizontalAdvance(QStringLiteral(" ")) * 4);
    editor_->setPlainText(QString());
    editor_->document()->setModified(false);
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &mainwindow::on_document_modified_changed);
    connect(editor_, &QPlainTextEdit::textChanged, this, &mainwindow::on_editor_text_changed);
    new latexhighlighter(editor_->document());
    update_window_title();

    preview_canvas_ = new pdfcanvas(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *right_splitter = new QSplitter(Qt::Horizontal, splitter);
    splitter->addWidget(editor_);

    auto *right_pane = new QWidget(this);
    right_pane->setObjectName("previewRightPane");
    auto *right_layout = new QVBoxLayout(right_pane);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(6);
    right_layout->addWidget(preview_canvas_, 1);

    auto *controls_row = new QWidget(right_pane);
    controls_row->setObjectName("previewControlsRow");
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

    auto *properties_scroll = new QScrollArea(right_splitter);
    properties_scroll->setWidgetResizable(true);
    properties_scroll->setFrameShape(QFrame::NoFrame);
    properties_scroll->setMinimumWidth(260);
    auto *properties_pane = new QWidget(properties_scroll);
    properties_pane->setObjectName("propertiesPane");
    auto *properties_layout = new QVBoxLayout(properties_pane);
    properties_layout->setContentsMargins(8, 8, 8, 8);
    properties_layout->setSpacing(6);
    auto *properties_title = new QLabel("Properties", properties_pane);
    QFont title_font = properties_title->font();
    title_font.setBold(true);
    properties_title->setFont(title_font);
    auto *selection_label = new QLabel("Selected Object", properties_pane);
    QFont selection_title_font = selection_label->font();
    selection_title_font.setBold(true);
    selection_label->setFont(selection_title_font);
    props_selection_value_ = new QLabel("None", properties_pane);
    props_selection_value_->setObjectName("selectionPill");
    props_selection_value_->setAlignment(Qt::AlignCenter);
    props_selection_value_->setStyleSheet(
        "padding:4px 8px; border:1px solid #cbd5e1; border-radius:8px; background:#f8fafc; color:#0f172a;");

    auto mk_spin = [properties_pane]() {
        auto *s = new QDoubleSpinBox(properties_pane);
        s->setRange(-1000000.0, 1000000.0);
        s->setDecimals(4);
        s->setSingleStep(0.1);
        return s;
    };
    props_label_1_ = new QLabel("Value 1", properties_pane);
    props_label_2_ = new QLabel("Value 2", properties_pane);
    props_label_3_ = new QLabel("Value 3", properties_pane);
    props_label_4_ = new QLabel("Value 4", properties_pane);
    props_value_1_ = mk_spin();
    props_value_2_ = mk_spin();
    props_value_3_ = mk_spin();
    props_value_4_ = mk_spin();
    connect(props_value_1_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        apply_selected_geometry_changes();
    });
    connect(props_value_2_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        apply_selected_geometry_changes();
    });
    connect(props_value_3_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        apply_selected_geometry_changes();
    });
    connect(props_value_4_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        apply_selected_geometry_changes();
    });
    auto *numeric_form = new QFormLayout;
    numeric_form->setContentsMargins(0, 0, 0, 0);
    numeric_form->addRow(props_label_1_, props_value_1_);
    numeric_form->addRow(props_label_2_, props_value_2_);
    numeric_form->addRow(props_label_3_, props_value_3_);
    numeric_form->addRow(props_label_4_, props_value_4_);
    auto *geometry_box = new QGroupBox("Geometry", properties_pane);
    auto *geometry_box_layout = new QVBoxLayout(geometry_box);
    geometry_box_layout->setContentsMargins(8, 8, 8, 8);
    geometry_box_layout->addLayout(numeric_form);

    props_color_combo_ = new QComboBox(properties_pane);
    props_color_combo_->addItems({"black", "blue", "red", "green", "orange", "magenta", "brown", "cyan", "gray", "yellow"});
    props_endpoint_start_combo_ = new QComboBox(properties_pane);
    props_endpoint_start_combo_->addItems({"none", "arrow", "bar"});
    props_endpoint_end_combo_ = new QComboBox(properties_pane);
    props_endpoint_end_combo_->addItems({"none", "arrow", "bar"});
    props_line_style_combo_ = new QComboBox(properties_pane);
    props_line_style_combo_->addItems({"solid",
                                       "dashed",
                                       "densely dashed",
                                       "loosely dashed",
                                       "dotted",
                                       "densely dotted",
                                       "loosely dotted",
                                       "dashdotted",
                                       "densely dashdotted",
                                       "loosely dashdotted"});
    props_thickness_combo_ = new QComboBox(properties_pane);
    props_thickness_combo_->addItems({"thin", "semithick", "thick", "very thick", "ultra thick"});
    props_draw_opacity_combo_ = new QComboBox(properties_pane);
    for (int i = 1; i <= 10; ++i) {
        const double v = static_cast<double>(i) / 10.0;
        props_draw_opacity_combo_->addItem(QString::number(v, 'f', 1), v);
    }
    props_fill_color_combo_ = new QComboBox(properties_pane);
    props_fill_color_combo_->addItems({"none", "black", "blue", "red", "green", "orange", "magenta", "brown", "cyan", "gray", "yellow"});
    props_fill_opacity_combo_ = new QComboBox(properties_pane);
    for (int i = 1; i <= 10; ++i) {
        const double v = static_cast<double>(i) / 10.0;
        props_fill_opacity_combo_->addItem(QString::number(v, 'f', 1), v);
    }
    connect(props_color_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_endpoint_start_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_endpoint_end_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_line_style_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_thickness_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_draw_opacity_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_fill_color_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_fill_opacity_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    auto *border_form = new QFormLayout;
    border_form->setContentsMargins(0, 0, 0, 0);
    border_form->addRow("Color", props_color_combo_);
    border_form->addRow("Endpoint start", props_endpoint_start_combo_);
    border_form->addRow("Endpoint end", props_endpoint_end_combo_);
    border_form->addRow("Style", props_line_style_combo_);
    border_form->addRow("Thickness", props_thickness_combo_);
    border_form->addRow("Opacity", props_draw_opacity_combo_);
    auto *border_box = new QGroupBox("Border", properties_pane);
    auto *border_box_layout = new QVBoxLayout(border_box);
    border_box_layout->setContentsMargins(8, 8, 8, 8);
    border_box_layout->addLayout(border_form);

    auto *fill_form = new QFormLayout;
    fill_form->setContentsMargins(0, 0, 0, 0);
    fill_form->addRow("Color", props_fill_color_combo_);
    fill_form->addRow("Opacity", props_fill_opacity_combo_);
    auto *fill_box = new QGroupBox("Fill", properties_pane);
    auto *fill_box_layout = new QVBoxLayout(fill_box);
    fill_box_layout->setContentsMargins(8, 8, 8, 8);
    fill_box_layout->addLayout(fill_form);
    properties_layout->addWidget(properties_title);
    properties_layout->addWidget(selection_label);
    properties_layout->addWidget(props_selection_value_);
    properties_layout->addWidget(geometry_box);
    properties_layout->addWidget(border_box);
    properties_layout->addWidget(fill_box);
    properties_layout->addStretch(1);
    properties_scroll->setWidget(properties_pane);

    right_splitter->addWidget(right_pane);
    right_splitter->addWidget(properties_scroll);
    right_splitter->setStretchFactor(0, 4);
    right_splitter->setStretchFactor(1, 1);
    right_splitter->setSizes({780, 250});

    splitter->addWidget(right_splitter);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({560, 840});

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
    QFont output_label_font = output_label->font();
    output_label_font.setBold(true);
    output_label->setFont(output_label_font);
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

    auto_compile_timer_ = new QTimer(this);
    auto_compile_timer_->setSingleShot(true);
    auto_compile_timer_->setInterval(auto_compile_delay_ms_);
    connect(auto_compile_timer_, &QTimer::timeout, this, &mainwindow::on_auto_compile_timeout);

    connect(preview_canvas_, &pdfcanvas::coordinate_dragged, this, &mainwindow::on_coordinate_dragged);
    connect(preview_canvas_, &pdfcanvas::circle_radius_dragged, this, &mainwindow::on_circle_radius_dragged);
    connect(preview_canvas_, &pdfcanvas::ellipse_radii_dragged, this, &mainwindow::on_ellipse_radii_dragged);
    connect(preview_canvas_, &pdfcanvas::bezier_control_dragged, this, &mainwindow::on_bezier_control_dragged);
    connect(preview_canvas_, &pdfcanvas::rectangle_corner_dragged, this, &mainwindow::on_rectangle_corner_dragged);
    connect(preview_canvas_, &pdfcanvas::selection_changed, this, &mainwindow::on_canvas_selection_changed);
    connect(grid_step_combo_, &QComboBox::currentIndexChanged, this, &mainwindow::on_grid_step_changed);
    connect(grid_extent_spin_, &QSpinBox::valueChanged, this, &mainwindow::on_grid_extent_changed);

    connect(compile_service_, &compileservice::output_text, this, &mainwindow::on_compile_service_output);
    connect(compile_service_, &compileservice::compile_finished, this, &mainwindow::on_compile_finished);

    preview_canvas_->set_snap_mm(grid_snap_mm_);
    load_settings();

    create_menu_and_toolbar();
    clear_properties_panel();
    statusBar()->showMessage("Ready");
}

void mainwindow::closeEvent(QCloseEvent *event) {
    if (maybe_save_before_action("Quit", "Save changes before quitting?")) {
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

bool mainwindow::maybe_save_before_action(const QString &title, const QString &text) {
    if (!editor_ || !editor_->document()->isModified()) {
        return true;
    }

    QMessageBox box(this);
    box.setWindowTitle(title);
    box.setText(text);
    box.setIcon(QMessageBox::Warning);
    QAbstractButton *save_btn = box.addButton("Save", QMessageBox::AcceptRole);
    QAbstractButton *discard_btn = box.addButton("Discard", QMessageBox::DestructiveRole);
    QAbstractButton *cancel_btn = box.addButton("Cancel", QMessageBox::RejectRole);
    box.setDefaultButton(static_cast<QPushButton *>(save_btn));
    box.exec();

    if (box.clickedButton() == cancel_btn) {
        return false;
    }
    if (box.clickedButton() == discard_btn) {
        return true;
    }
    save_file();
    return !editor_->document()->isModified();
}

void mainwindow::request_compile(bool cancel_running) {
    if (!compile_service_ || !editor_) {
        return;
    }
    if (compile_service_->is_busy()) {
        pending_compile_ = true;
        if (cancel_running) {
            compile_service_->cancel();
        }
        return;
    }

    const QString source_text = editor_->toPlainText();
    coordinate_refs_ = coordinateparser::extract_refs(source_text);
    circle_refs_ = coordinateparser::extract_circle_refs(source_text);
    ellipse_refs_ = coordinateparser::extract_ellipse_refs(source_text);
    bezier_refs_ = coordinateparser::extract_bezier_refs(source_text);
    rectangle_refs_ = coordinateparser::extract_rectangle_refs(source_text);
    update_properties_panel();
    preview_canvas_->set_coordinates(coordinateparser::extract_pairs(source_text));
    preview_canvas_->set_circles(coordinateparser::extract_circle_pairs(source_text));
    preview_canvas_->set_ellipses(coordinateparser::extract_ellipse_pairs(source_text));
    preview_canvas_->set_beziers(coordinateparser::extract_bezier_pairs(source_text));
    preview_canvas_->set_rectangles(coordinateparser::extract_rectangle_pairs(source_text));
    compile_service_->compile(source_text, grid_display_mm_, grid_extent_cm_);
    statusBar()->showMessage("Compiling...");
}

void mainwindow::replace_editor_text_preserve_undo(const QString &text) {
    suppress_auto_compile_ = true;
    editor_->setPlainText(text);
    suppress_auto_compile_ = false;
}

void mainwindow::on_editor_text_changed() {
    update_properties_panel();
    if (suppress_auto_compile_) {
        return;
    }
    if (auto_compile_timer_) {
        auto_compile_timer_->start();
    }
}

void mainwindow::on_auto_compile_timeout() {
    request_compile(true);
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
    auto *settings_act = new QAction(QIcon::fromTheme("preferences-system"), "Settings...", this);
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
    settings_act->setShortcut(QKeySequence("Ctrl+,"));
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
    connect(settings_act, &QAction::triggered, this, &mainwindow::open_settings);
    connect(compile_act, &QAction::triggered, this, &mainwindow::compile);
    connect(quit_act, &QAction::triggered, this, &QWidget::close);
    connect(about_ktikz_act, &QAction::triggered, this, [this]() {
        const QString app_name = QString::fromLatin1(appconfig::APP_NAME);
        QMessageBox::about(
            this,
            "About " + app_name,
            app_name + "\n\n"
            "A Qt editor for TikZ with live PDF preview and interactive shape editing.\n\n"
            "Francesco Betti Sorbelli <francesco.bettisorbelli@unipg.it>");
    });

    auto add_example = [this, examples_menu](const QString &label, const QString &body) {
        auto *act = new QAction(label, this);
        connect(act, &QAction::triggered, this, [this, body, label]() {
            if (!editor_) {
                return;
            }
            replace_editor_text_preserve_undo(wrap_tikz_document(body));
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
    edit_menu->addSeparator();
    edit_menu->addAction(settings_act);
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

void mainwindow::apply_editor_font_size(int size) {
    editor_font_size_ = qBound(8, size, 32);
    if (!editor_) {
        return;
    }
    QFont font = editor_->font();
    font.setPointSize(editor_font_size_);
    editor_->setFont(font);
}

void mainwindow::apply_editor_font_family(const QString &family) {
    const QString normalized = family.trimmed();
    if (!normalized.isEmpty()) {
        editor_font_family_ = normalized;
    }
    if (!editor_) {
        return;
    }
    QFont font = editor_->font();
    font.setFamily(editor_font_family_);
    font.setStyleHint(QFont::Monospace);
    editor_->setFont(font);
}

void mainwindow::apply_line_number_visibility(bool visible) {
    show_line_numbers_ = visible;
    if (!editor_) {
        return;
    }
    auto *line_editor = static_cast<linenumberedit *>(editor_);
    line_editor->set_line_numbers_visible(show_line_numbers_);
}

void mainwindow::apply_theme(const QString &theme_id) {
    const QString normalized = theme_id.trimmed().isEmpty() ? QStringLiteral("system") : theme_id.trimmed();
    theme_id_ = normalized;

    if (!qApp) {
        return;
    }
    static const QString startup_style_name = qApp->style() ? qApp->style()->objectName() : QString();
    static const QPalette startup_palette = qApp->palette();

    if (theme_id_ == "dark") {
        qApp->setStyle(QStyleFactory::create("Fusion"));
        qApp->setPalette(make_dark_palette());
        qApp->setStyleSheet(
            "QMenuBar{background-color:#2d2d2d;color:#e6e6e6;}"
            "QMenuBar::item:selected{background-color:#3a3a3a;}"
            "QMenu{background-color:#2d2d2d;color:#e6e6e6;border:1px solid #3f3f3f;}"
            "QMenu::item:selected{background-color:#3d6ea8;}"
            "QToolBar{background-color:#2d2d2d;border-bottom:1px solid #3f3f3f;}"
            "QStatusBar{background-color:#2d2d2d;color:#e6e6e6;}"
            "QLabel{color:#e6e6e6;}"
            "QWidget#previewRightPane{background-color:#2d2d2d;}"
            "QWidget#previewControlsRow{background-color:#2d2d2d;}"
            "QWidget#propertiesPane{background-color:#2d2d2d;}"
            "QLabel#selectionPill{background-color:#1f2937;color:#e5e7eb;border:1px solid #4b5563;border-radius:8px;padding:4px 8px;}");
        return;
    }

    if (theme_id_ == "light") {
        if (!startup_style_name.isEmpty()) {
            qApp->setStyle(QStyleFactory::create(startup_style_name));
        }
        qApp->setPalette(startup_palette);
        qApp->setStyleSheet(QString());
        return;
    }

    if (!startup_style_name.isEmpty()) {
        qApp->setStyle(QStyleFactory::create(startup_style_name));
    }
    qApp->setPalette(startup_palette);
    qApp->setStyleSheet(QString());
}

void mainwindow::load_settings() {
    QSettings settings;
    const QString saved_font_family = settings.value("ui/editor_font_family", editor_font_family_).toString();
    const int saved_font_size = settings.value("ui/editor_font_size", editor_font_size_).toInt();
    const bool saved_line_numbers = settings.value("ui/show_line_numbers", show_line_numbers_).toBool();
    const QString saved_theme = settings.value("ui/theme", theme_id_).toString();
    const int saved_delay_ms = settings.value("build/auto_compile_delay_ms", auto_compile_delay_ms_).toInt();
    const QString saved_compiler = settings.value("build/compiler_command", compiler_command_).toString();
    const int saved_step_mm = settings.value("grid/step_mm", grid_snap_mm_).toInt();
    const int saved_extent_cm = settings.value("grid/extent_cm", grid_extent_cm_).toInt();

    apply_editor_font_family(saved_font_family);
    apply_editor_font_size(saved_font_size);
    apply_line_number_visibility(saved_line_numbers);
    apply_theme(saved_theme);
    auto_compile_delay_ms_ = qBound(100, saved_delay_ms, 3000);
    if (auto_compile_timer_) {
        auto_compile_timer_->setInterval(auto_compile_delay_ms_);
    }
    compiler_command_ = saved_compiler.trimmed().isEmpty() ? QStringLiteral("pdflatex") : saved_compiler.trimmed();
    if (compile_service_) {
        compile_service_->set_compiler_command(compiler_command_);
    }

    grid_extent_cm_ = qBound(20, saved_extent_cm, 100);
    if (grid_extent_spin_) {
        const QSignalBlocker blocker(grid_extent_spin_);
        grid_extent_spin_->setValue(grid_extent_cm_);
    }

    int normalized_step = saved_step_mm;
    if (normalized_step != 10 && normalized_step != 5 && normalized_step != 2 && normalized_step != 1 &&
        normalized_step != 0) {
        normalized_step = 10;
    }
    grid_snap_mm_ = normalized_step;
    grid_display_mm_ = (grid_snap_mm_ == 0) ? 10 : grid_snap_mm_;
    preview_canvas_->set_snap_mm(grid_snap_mm_);
    if (grid_step_combo_) {
        const QSignalBlocker blocker(grid_step_combo_);
        const int idx = grid_step_combo_->findData(grid_snap_mm_);
        grid_step_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

void mainwindow::save_settings() const {
    QSettings settings;
    settings.setValue("ui/editor_font_family", editor_font_family_);
    settings.setValue("ui/editor_font_size", editor_font_size_);
    settings.setValue("ui/show_line_numbers", show_line_numbers_);
    settings.setValue("ui/theme", theme_id_);
    settings.setValue("build/auto_compile_delay_ms", auto_compile_delay_ms_);
    settings.setValue("build/compiler_command", compiler_command_);
    settings.setValue("grid/step_mm", grid_snap_mm_);
    settings.setValue("grid/extent_cm", grid_extent_cm_);
}

void mainwindow::open_settings() {
    settingsdialog dialog(this);
    dialog.set_editor_font_family(editor_font_family_);
    dialog.set_editor_font_size(editor_font_size_);
    dialog.set_show_line_numbers(show_line_numbers_);
    dialog.set_theme(theme_id_);
    dialog.set_auto_compile_delay_ms(auto_compile_delay_ms_);
    dialog.set_compiler_command(compiler_command_);
    dialog.set_grid_step_mm(grid_snap_mm_);
    dialog.set_grid_extent_cm(grid_extent_cm_);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    apply_editor_font_family(dialog.editor_font_family());
    apply_editor_font_size(dialog.editor_font_size());
    apply_line_number_visibility(dialog.show_line_numbers());
    apply_theme(dialog.theme());
    auto_compile_delay_ms_ = qBound(100, dialog.auto_compile_delay_ms(), 3000);
    if (auto_compile_timer_) {
        auto_compile_timer_->setInterval(auto_compile_delay_ms_);
    }
    compiler_command_ = dialog.compiler_command().trimmed().isEmpty() ? QStringLiteral("pdflatex")
                                                                      : dialog.compiler_command().trimmed();
    if (compile_service_) {
        compile_service_->set_compiler_command(compiler_command_);
    }

    grid_extent_cm_ = qBound(20, dialog.grid_extent_cm(), 100);
    if (grid_extent_spin_) {
        const QSignalBlocker blocker(grid_extent_spin_);
        grid_extent_spin_->setValue(grid_extent_cm_);
    }

    const int new_step = dialog.grid_step_mm();
    grid_snap_mm_ = qMax(0, new_step);
    grid_display_mm_ = (grid_snap_mm_ == 0) ? 10 : grid_snap_mm_;
    preview_canvas_->set_snap_mm(grid_snap_mm_);
    if (grid_step_combo_) {
        const QSignalBlocker blocker(grid_step_combo_);
        const int idx = grid_step_combo_->findData(grid_snap_mm_);
        grid_step_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    save_settings();
    request_compile(true);
    statusBar()->showMessage("Settings updated", 2000);
}

void mainwindow::new_file() {
    if (!editor_) {
        return;
    }

    if (!maybe_save_before_action("New file", "Save changes before creating a new file?")) {
        return;
    }

    replace_editor_text_preserve_undo(QString());
    current_file_path_.clear();
    editor_->document()->setModified(false);
    update_window_title();
    statusBar()->showMessage("New file", 1500);
}

void mainwindow::load_file() {
    if (!maybe_save_before_action("Open file", "Save changes before opening another file?")) {
        return;
    }

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
    replace_editor_text_preserve_undo(in.readAll());
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
    request_compile(true);
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
    const QString lower = text.toLower();
    const bool has_error = lower.contains("error") || lower.contains("failed");
    const QColor color = has_error
                             ? (theme_id_ == "dark" ? QColor("#f87171") : QColor("#b91c1c"))
                             : palette().color(QPalette::Text);
    append_colored_log(output_, text, color);
}

void mainwindow::on_compile_finished(bool success, const QString &pdf_path, const QString &message) {
    if (message != "canceled") {
        if (!success) {
            append_colored_log(
                output_, "[Status] Compiled with errors", theme_id_ == "dark" ? QColor("#f87171") : QColor("#dc2626"));
            statusBar()->showMessage("Compile failed", 3000);
        } else if (!preview_canvas_->load_pdf(pdf_path)) {
            on_compile_service_output("[Preview] Failed to load generated PDF");
            append_colored_log(
                output_, "[Status] Compiled with errors", theme_id_ == "dark" ? QColor("#f87171") : QColor("#dc2626"));
            statusBar()->showMessage("Preview load failed", 3000);
        } else {
            append_colored_log(
                output_, "[Status] Compiled successfully", theme_id_ == "dark" ? QColor("#86efac") : QColor("#16a34a"));
            statusBar()->showMessage("Compile successful", 2500);
        }
    }

    if (pending_compile_) {
        pending_compile_ = false;
        request_compile(false);
    }
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
    replace_editor_text_preserve_undo(text);
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
    replace_editor_text_preserve_undo(text);
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

    replace_editor_text_preserve_undo(text);
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
    replace_editor_text_preserve_undo(text);
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

    replace_editor_text_preserve_undo(text);
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

    request_compile(true);
}

void mainwindow::on_grid_extent_changed(int value) {
    grid_extent_cm_ = qBound(20, value, 100);
    statusBar()->showMessage("Grid extent: " + QString::number(grid_extent_cm_) + " cm", 1500);
    request_compile(true);
}

void mainwindow::on_canvas_selection_changed(const QString &type, int index, int subindex) {
    selected_type_ = type;
    selected_index_ = index;
    selected_subindex_ = subindex;
    update_properties_panel();
}

bool mainwindow::replace_segments(QString &text, const std::vector<std::tuple<int, int, QString>> &segments) {
    std::vector<std::tuple<int, int, QString>> sorted = segments;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return std::get<0>(a) > std::get<0>(b); });
    for (const auto &[start, end, replacement] : sorted) {
        if (start < 0 || end <= start || end > text.size()) {
            return false;
        }
        text.replace(start, end - start, replacement);
    }
    return true;
}

int mainwindow::selected_anchor_position() const {
    if (selected_index_ < 0) {
        return -1;
    }
    if (selected_type_ == "coordinate" && selected_index_ < static_cast<int>(coordinate_refs_.size())) {
        return coordinate_refs_[selected_index_].start;
    }
    if (selected_type_ == "circle" && selected_index_ < static_cast<int>(circle_refs_.size())) {
        return circle_refs_[selected_index_].radius_start;
    }
    if (selected_type_ == "ellipse" && selected_index_ < static_cast<int>(ellipse_refs_.size())) {
        return ellipse_refs_[selected_index_].rx_start;
    }
    if (selected_type_ == "rectangle" && selected_index_ < static_cast<int>(rectangle_refs_.size())) {
        return rectangle_refs_[selected_index_].x2_start;
    }
    if (selected_type_ == "bezier" && selected_index_ < static_cast<int>(bezier_refs_.size())) {
        return bezier_refs_[selected_index_].x1_start;
    }
    return -1;
}

bool mainwindow::selected_command_span(int &start_out, int &end_out) const {
    if (!editor_) {
        return false;
    }
    const int anchor = selected_anchor_position();
    if (anchor < 0) {
        return false;
    }
    const QString text = editor_->toPlainText();
    static const QRegularExpression drawable_cmd(R"(\\(?:draw|node)(?:\s*\[[^\]]*\])?[\s\S]*?;)");
    QRegularExpressionMatchIterator it = drawable_cmd.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int s = m.capturedStart(0);
        const int e = s + m.capturedLength(0);
        if (anchor >= s && anchor < e) {
            start_out = s;
            end_out = e;
            return true;
        }
    }
    return false;
}

void mainwindow::clear_properties_panel(const QString &message) {
    if (props_selection_value_) {
        props_selection_value_->setText("None");
    }
    auto disable_spin = [](QLabel *lbl, QDoubleSpinBox *spin) {
        if (lbl) {
            lbl->hide();
        }
        if (spin) {
            spin->hide();
            spin->setEnabled(false);
        }
    };
    disable_spin(props_label_1_, props_value_1_);
    disable_spin(props_label_2_, props_value_2_);
    disable_spin(props_label_3_, props_value_3_);
    disable_spin(props_label_4_, props_value_4_);
    if (props_color_combo_) {
        const QSignalBlocker blocker(props_color_combo_);
        props_color_combo_->setCurrentIndex(0);
        props_color_combo_->setEnabled(!selected_type_.isEmpty());
    }
    if (props_endpoint_start_combo_) {
        const QSignalBlocker blocker(props_endpoint_start_combo_);
        props_endpoint_start_combo_->setCurrentIndex(0);
        props_endpoint_start_combo_->setEnabled(!selected_type_.isEmpty());
    }
    if (props_endpoint_end_combo_) {
        const QSignalBlocker blocker(props_endpoint_end_combo_);
        props_endpoint_end_combo_->setCurrentIndex(0);
        props_endpoint_end_combo_->setEnabled(!selected_type_.isEmpty());
    }
    if (props_line_style_combo_) {
        const QSignalBlocker blocker(props_line_style_combo_);
        props_line_style_combo_->setCurrentIndex(0);
        props_line_style_combo_->setEnabled(!selected_type_.isEmpty());
    }
    if (props_thickness_combo_) {
        const QSignalBlocker blocker(props_thickness_combo_);
        props_thickness_combo_->setCurrentIndex(0);
        props_thickness_combo_->setEnabled(!selected_type_.isEmpty());
    }
    if (props_draw_opacity_combo_) {
        const QSignalBlocker blocker(props_draw_opacity_combo_);
        props_draw_opacity_combo_->setCurrentIndex(props_draw_opacity_combo_->count() - 1);
        props_draw_opacity_combo_->setEnabled(!selected_type_.isEmpty());
    }
    if (props_fill_color_combo_) {
        const QSignalBlocker blocker(props_fill_color_combo_);
        props_fill_color_combo_->setCurrentIndex(0);
        props_fill_color_combo_->setEnabled(!selected_type_.isEmpty());
    }
    if (props_fill_opacity_combo_) {
        const QSignalBlocker blocker(props_fill_opacity_combo_);
        props_fill_opacity_combo_->setCurrentIndex(props_fill_opacity_combo_->count() - 1);
        props_fill_opacity_combo_->setEnabled(!selected_type_.isEmpty());
    }
    Q_UNUSED(message)
}

void mainwindow::update_properties_panel() {
    if (!props_selection_value_) {
        return;
    }
    if (selected_type_.isEmpty() || selected_index_ < 0) {
        clear_properties_panel();
        return;
    }

    auto show_spin = [](QLabel *lbl, QDoubleSpinBox *spin, const QString &name, double value) {
        if (!lbl || !spin) {
            return;
        }
        lbl->setText(name);
        lbl->show();
        spin->show();
        spin->setEnabled(true);
        const QSignalBlocker blocker(spin);
        spin->setValue(value);
    };

    suppress_properties_apply_ = true;
    clear_properties_panel();
    if (props_color_combo_) {
        props_color_combo_->setEnabled(true);
    }
    if (props_line_style_combo_) {
        props_line_style_combo_->setEnabled(true);
    }
    if (props_endpoint_start_combo_) {
        props_endpoint_start_combo_->setEnabled(true);
    }
    if (props_endpoint_end_combo_) {
        props_endpoint_end_combo_->setEnabled(true);
    }
    if (props_thickness_combo_) {
        props_thickness_combo_->setEnabled(true);
    }
    if (props_draw_opacity_combo_) {
        props_draw_opacity_combo_->setEnabled(true);
    }
    if (props_fill_color_combo_) {
        props_fill_color_combo_->setEnabled(true);
    }
    if (props_fill_opacity_combo_) {
        props_fill_opacity_combo_->setEnabled(true);
    }

    int cmd_start = 0;
    int cmd_end = 0;
    if (selected_command_span(cmd_start, cmd_end) && editor_) {
        const QString cmd = editor_->toPlainText().mid(cmd_start, cmd_end - cmd_start);
        static const QRegularExpression draw_head(R"(^(\s*\\(?:draw|node))\s*(\[[^\]]*\])?)");
        const QRegularExpressionMatch m = draw_head.match(cmd);
        if (m.hasMatch() && m.capturedStart(2) >= 0) {
            QString opts_text = m.captured(2);
            opts_text.remove(0, 1);
            opts_text.chop(1);
            QStringList opts = opts_text.split(',', Qt::SkipEmptyParts);
            for (QString &v : opts) {
                v = v.trimmed();
            }

            auto find_prefix_value = [&opts](const QString &prefix) -> QString {
                for (const QString &v : opts) {
                    if (v.startsWith(prefix)) {
                        return v.mid(prefix.size()).trimmed();
                    }
                }
                return QString();
            };
            auto has_token = [&opts](const QString &token) { return opts.contains(token); };
            auto set_combo_value = [](QComboBox *combo, const QString &value) {
                if (!combo || value.isEmpty()) {
                    return;
                }
                int idx = combo->findText(value);
                if (idx < 0) {
                    combo->addItem(value);
                    idx = combo->findText(value);
                }
                const QSignalBlocker blocker(combo);
                combo->setCurrentIndex(qMax(0, idx));
            };
            auto set_opacity_combo = [](QComboBox *combo, double value) {
                if (!combo) {
                    return;
                }
                const double clamped = qBound(0.1, value, 1.0);
                const QString label = QString::number(std::round(clamped * 10.0) / 10.0, 'f', 1);
                int idx = combo->findText(label);
                if (idx < 0) {
                    idx = combo->count() - 1;
                }
                const QSignalBlocker blocker(combo);
                combo->setCurrentIndex(idx);
            };

            const QString command_name = m.captured(1).contains("\\node") ? QStringLiteral("node") : QStringLiteral("draw");
            const QString explicit_draw = (command_name == "node") ? find_prefix_value("text=") : find_prefix_value("draw=");
            QString draw_color = explicit_draw;
            if (draw_color.isEmpty()) {
                if (command_name == "node") {
                    draw_color = find_prefix_value("color=");
                }
            }
            if (draw_color.isEmpty()) {
                const QStringList colors = {"black", "blue", "red", "green", "orange", "magenta", "brown", "cyan", "gray", "yellow"};
                for (const QString &c : colors) {
                    if (has_token(c)) {
                        draw_color = c;
                        break;
                    }
                }
            }
            if (draw_color.isEmpty()) {
                draw_color = "black";
            }
            set_combo_value(props_color_combo_, draw_color);

            QString style = "solid";
            const QStringList styles = {"loosely dashdotted",
                                        "densely dashdotted",
                                        "dashdotted",
                                        "loosely dashed",
                                        "densely dashed",
                                        "dashed",
                                        "loosely dotted",
                                        "densely dotted",
                                        "dotted"};
            for (const QString &s : styles) {
                if (has_token(s)) {
                    style = s;
                    break;
                }
            }
            set_combo_value(props_line_style_combo_, style);

            QString endpoint = "-";
            const QStringList endpoints = {"<->", "|->", "<-|", "|-|", "->", "<-", "-|", "|-"};
            for (const QString &ep : endpoints) {
                if (has_token(ep)) {
                    endpoint = ep;
                    break;
                }
            }
            QString start_cap = "none";
            QString end_cap = "none";
            if (endpoint == "<->") {
                start_cap = "arrow";
                end_cap = "arrow";
            } else if (endpoint == "->") {
                end_cap = "arrow";
            } else if (endpoint == "<-") {
                start_cap = "arrow";
            } else if (endpoint == "|->") {
                start_cap = "bar";
                end_cap = "arrow";
            } else if (endpoint == "<-|") {
                start_cap = "arrow";
                end_cap = "bar";
            } else if (endpoint == "|-|") {
                start_cap = "bar";
                end_cap = "bar";
            } else if (endpoint == "-|") {
                end_cap = "bar";
            } else if (endpoint == "|-") {
                start_cap = "bar";
            }
            set_combo_value(props_endpoint_start_combo_, start_cap);
            set_combo_value(props_endpoint_end_combo_, end_cap);

            QString thick = "thin";
            const QStringList thicknesses = {"ultra thick", "very thick", "thick", "semithick", "thin"};
            for (const QString &t : thicknesses) {
                if (has_token(t)) {
                    thick = t;
                    break;
                }
            }
            set_combo_value(props_thickness_combo_, thick);

            bool ok_draw_op = false;
            const double draw_op = find_prefix_value("draw opacity=").toDouble(&ok_draw_op);
            set_opacity_combo(props_draw_opacity_combo_, ok_draw_op ? draw_op : 1.0);

            QString fill = find_prefix_value("fill=");
            if (fill.isEmpty()) {
                fill = "none";
            }
            if (fill == "none") {
                set_combo_value(props_fill_color_combo_, "none");
            } else {
                set_combo_value(props_fill_color_combo_, fill);
            }

            bool ok_fill_op = false;
            const double fill_op = find_prefix_value("fill opacity=").toDouble(&ok_fill_op);
            set_opacity_combo(props_fill_opacity_combo_, ok_fill_op ? fill_op : 1.0);

            if (command_name == "node" && props_endpoint_start_combo_ && props_endpoint_end_combo_) {
                const QSignalBlocker blocker1(props_endpoint_start_combo_);
                const QSignalBlocker blocker2(props_endpoint_end_combo_);
                props_endpoint_start_combo_->setCurrentIndex(0);
                props_endpoint_end_combo_->setCurrentIndex(0);
                props_endpoint_start_combo_->setEnabled(false);
                props_endpoint_end_combo_->setEnabled(false);
            }
        }
    }

    if (selected_type_ == "coordinate") {
        if (selected_index_ >= static_cast<int>(coordinate_refs_.size())) {
            clear_properties_panel();
            return;
        }
        const coord_ref &c = coordinate_refs_[selected_index_];
        props_selection_value_->setText("Coordinate #" + QString::number(selected_index_ + 1));
        show_spin(props_label_1_, props_value_1_, "x", c.x);
        show_spin(props_label_2_, props_value_2_, "y", c.y);
        suppress_properties_apply_ = false;
        return;
    }

    if (selected_type_ == "circle") {
        if (selected_index_ >= static_cast<int>(circle_refs_.size())) {
            clear_properties_panel();
            return;
        }
        const circle_ref &c = circle_refs_[selected_index_];
        props_selection_value_->setText("Circle #" + QString::number(selected_index_ + 1));
        show_spin(props_label_1_, props_value_1_, "center x", c.cx);
        show_spin(props_label_2_, props_value_2_, "center y", c.cy);
        show_spin(props_label_3_, props_value_3_, "radius", c.r);
        suppress_properties_apply_ = false;
        return;
    }

    if (selected_type_ == "ellipse") {
        if (selected_index_ >= static_cast<int>(ellipse_refs_.size())) {
            clear_properties_panel();
            return;
        }
        const ellipse_ref &e = ellipse_refs_[selected_index_];
        props_selection_value_->setText("Ellipse #" + QString::number(selected_index_ + 1));
        show_spin(props_label_1_, props_value_1_, "center x", e.cx);
        show_spin(props_label_2_, props_value_2_, "center y", e.cy);
        show_spin(props_label_3_, props_value_3_, "rx", e.rx);
        show_spin(props_label_4_, props_value_4_, "ry", e.ry);
        suppress_properties_apply_ = false;
        return;
    }

    if (selected_type_ == "rectangle") {
        if (selected_index_ >= static_cast<int>(rectangle_refs_.size())) {
            clear_properties_panel();
            return;
        }
        const rectangle_ref &r = rectangle_refs_[selected_index_];
        props_selection_value_->setText("Rectangle #" + QString::number(selected_index_ + 1));
        show_spin(props_label_1_, props_value_1_, "x1", r.x1);
        show_spin(props_label_2_, props_value_2_, "y1", r.y1);
        show_spin(props_label_3_, props_value_3_, "x2", r.x2);
        show_spin(props_label_4_, props_value_4_, "y2", r.y2);
        suppress_properties_apply_ = false;
        return;
    }

    if (selected_type_ == "bezier") {
        if (selected_index_ >= static_cast<int>(bezier_refs_.size())) {
            clear_properties_panel();
            return;
        }
        const bezier_ref &b = bezier_refs_[selected_index_];
        props_selection_value_->setText(
            "Bezier #" + QString::number(selected_index_ + 1) +
            (selected_subindex_ == 1 ? " (control 1)" : (selected_subindex_ == 2 ? " (control 2)" : "")));
        show_spin(props_label_1_, props_value_1_, "c1 x", b.x1);
        show_spin(props_label_2_, props_value_2_, "c1 y", b.y1);
        show_spin(props_label_3_, props_value_3_, "c2 x", b.x2);
        show_spin(props_label_4_, props_value_4_, "c2 y", b.y2);
        suppress_properties_apply_ = false;
        return;
    }

    clear_properties_panel();
    suppress_properties_apply_ = false;
}

void mainwindow::apply_selected_geometry_changes() {
    if (suppress_properties_apply_ || !editor_ || selected_type_.isEmpty() || selected_index_ < 0) {
        return;
    }

    QString text = editor_->toPlainText();
    std::vector<std::tuple<int, int, QString>> segments;
    auto num = [](QDoubleSpinBox *s) { return coordinateparser::format_number(s ? s->value() : 0.0); };

    if (selected_type_ == "coordinate" && selected_index_ < static_cast<int>(coordinate_refs_.size())) {
        const coord_ref &r = coordinate_refs_[selected_index_];
        segments.push_back({r.x_start, r.x_end, num(props_value_1_)});
        segments.push_back({r.y_start, r.y_end, num(props_value_2_)});
    } else if (selected_type_ == "circle" && selected_index_ < static_cast<int>(circle_refs_.size())) {
        const circle_ref &r = circle_refs_[selected_index_];
        segments.push_back({r.cx_start, r.cx_end, num(props_value_1_)});
        segments.push_back({r.cy_start, r.cy_end, num(props_value_2_)});
        segments.push_back({r.radius_start, r.radius_end, num(props_value_3_)});
    } else if (selected_type_ == "ellipse" && selected_index_ < static_cast<int>(ellipse_refs_.size())) {
        const ellipse_ref &r = ellipse_refs_[selected_index_];
        segments.push_back({r.cx_start, r.cx_end, num(props_value_1_)});
        segments.push_back({r.cy_start, r.cy_end, num(props_value_2_)});
        segments.push_back({r.rx_start, r.rx_end, num(props_value_3_)});
        segments.push_back({r.ry_start, r.ry_end, num(props_value_4_)});
    } else if (selected_type_ == "rectangle" && selected_index_ < static_cast<int>(rectangle_refs_.size())) {
        const rectangle_ref &r = rectangle_refs_[selected_index_];
        segments.push_back({r.x1_start, r.x1_end, num(props_value_1_)});
        segments.push_back({r.y1_start, r.y1_end, num(props_value_2_)});
        segments.push_back({r.x2_start, r.x2_end, num(props_value_3_)});
        segments.push_back({r.y2_start, r.y2_end, num(props_value_4_)});
    } else if (selected_type_ == "bezier" && selected_index_ < static_cast<int>(bezier_refs_.size())) {
        const bezier_ref &r = bezier_refs_[selected_index_];
        segments.push_back({r.x1_start, r.x1_end, num(props_value_1_)});
        segments.push_back({r.y1_start, r.y1_end, num(props_value_2_)});
        segments.push_back({r.x2_start, r.x2_end, num(props_value_3_)});
        segments.push_back({r.y2_start, r.y2_end, num(props_value_4_)});
    } else {
        return;
    }

    if (!replace_segments(text, segments)) {
        return;
    }
    replace_editor_text_preserve_undo(text);
    request_compile(true);
}

void mainwindow::apply_selected_style_changes() {
    if (suppress_properties_apply_ || !editor_ || selected_type_.isEmpty() || selected_index_ < 0) {
        return;
    }
    int cmd_start = 0;
    int cmd_end = 0;
    if (!selected_command_span(cmd_start, cmd_end)) {
        return;
    }

    QString text = editor_->toPlainText();
    QString cmd = text.mid(cmd_start, cmd_end - cmd_start);
    static const QRegularExpression draw_head(R"(^(\s*\\(?:draw|node))\s*(\[[^\]]*\])?)");
    const QRegularExpressionMatch m = draw_head.match(cmd);
    if (!m.hasMatch()) {
        return;
    }
    const bool is_node_command = m.captured(1).contains("\\node");

    QStringList opts;
    if (m.capturedStart(2) >= 0) {
        QString o = m.captured(2);
        o.remove(0, 1);
        o.chop(1);
        opts = o.split(',', Qt::SkipEmptyParts);
        for (QString &v : opts) {
            v = v.trimmed();
        }
    }

    auto remove_tokens = [&opts](const QStringList &to_remove) {
        opts.erase(
            std::remove_if(opts.begin(), opts.end(), [&to_remove](const QString &v) { return to_remove.contains(v); }),
            opts.end());
    };
    auto remove_prefix = [&opts](const QString &prefix) {
        opts.erase(std::remove_if(opts.begin(), opts.end(), [&prefix](const QString &v) { return v.startsWith(prefix); }),
                   opts.end());
    };

    const QString color = props_color_combo_ ? props_color_combo_->currentText() : QString("black");
    remove_tokens({"black", "blue", "red", "green", "orange", "magenta", "brown", "cyan", "gray", "yellow"});
    if (is_node_command) {
        remove_prefix("text=");
        remove_prefix("color=");
        opts.push_back("text=" + color);
    } else {
        remove_prefix("draw=");
        opts.push_back("draw=" + color);
    }

    if (!is_node_command) {
        const QString start_cap = props_endpoint_start_combo_ ? props_endpoint_start_combo_->currentText() : QString("none");
        const QString end_cap = props_endpoint_end_combo_ ? props_endpoint_end_combo_->currentText() : QString("none");
        remove_tokens({"<->", "|->", "<-|", "|-|", "->", "<-", "-|", "|-", "-"});
        QString endpoint = "-";
        if (start_cap == "arrow" && end_cap == "arrow") {
            endpoint = "<->";
        } else if (start_cap == "arrow" && end_cap == "bar") {
            endpoint = "<-|";
        } else if (start_cap == "bar" && end_cap == "arrow") {
            endpoint = "|->";
        } else if (start_cap == "bar" && end_cap == "bar") {
            endpoint = "|-|";
        } else if (start_cap == "arrow") {
            endpoint = "<-";
        } else if (end_cap == "arrow") {
            endpoint = "->";
        } else if (start_cap == "bar") {
            endpoint = "|-";
        } else if (end_cap == "bar") {
            endpoint = "-|";
        }
        if (endpoint != "-") {
            opts.push_back(endpoint);
        }
    }

    const QString line_style = props_line_style_combo_ ? props_line_style_combo_->currentText() : QString("solid");
    remove_tokens({"dashed",
                   "densely dashed",
                   "loosely dashed",
                   "dotted",
                   "densely dotted",
                   "loosely dotted",
                   "dashdotted",
                   "densely dashdotted",
                   "loosely dashdotted",
                   "solid"});
    if (line_style != "solid") {
        opts.push_back(line_style);
    }

    const QString thickness = props_thickness_combo_ ? props_thickness_combo_->currentText() : QString("thin");
    remove_tokens({"ultra thin", "very thin", "thin", "semithick", "thick", "very thick", "ultra thick"});
    opts.push_back(thickness);

    const double draw_opacity = props_draw_opacity_combo_ ? props_draw_opacity_combo_->currentData().toDouble() : 1.0;
    remove_prefix("draw opacity=");
    opts.push_back("draw opacity=" + coordinateparser::format_number(draw_opacity));

    const QString fill_color = props_fill_color_combo_ ? props_fill_color_combo_->currentText() : QString("none");
    remove_prefix("fill=");
    if (fill_color == "none") {
        opts.push_back("fill=none");
    } else {
        opts.push_back("fill=" + fill_color);
    }

    const double fill_opacity = props_fill_opacity_combo_ ? props_fill_opacity_combo_->currentData().toDouble() : 1.0;
    remove_prefix("fill opacity=");
    opts.push_back("fill opacity=" + coordinateparser::format_number(fill_opacity));

    const QString new_head = m.captured(1) + (opts.isEmpty() ? QString() : "[" + opts.join(",") + "]");
    cmd.replace(0, m.capturedLength(0), new_head);
    text.replace(cmd_start, cmd_end - cmd_start, cmd);
    replace_editor_text_preserve_undo(text);
    request_compile(true);
}
