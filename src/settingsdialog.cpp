#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

settingsdialog::settingsdialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Settings");
    resize(460, 320);

    editor_font_family_combo_ = new QFontComboBox(this);

    editor_font_size_spin_ = new QSpinBox(this);
    editor_font_size_spin_->setRange(8, 32);
    editor_font_size_spin_->setValue(12);

    show_line_numbers_check_ = new QCheckBox("Show line numbers", this);
    show_line_numbers_check_->setChecked(true);

    auto_compile_delay_spin_ = new QSpinBox(this);
    auto_compile_delay_spin_->setRange(100, 3000);
    auto_compile_delay_spin_->setSingleStep(50);
    auto_compile_delay_spin_->setSuffix(" ms");
    auto_compile_delay_spin_->setValue(450);

    compiler_command_edit_ = new QLineEdit(this);
    compiler_command_edit_->setPlaceholderText("pdflatex");
    auto *compiler_browse_btn = new QPushButton("Browse...", this);
    connect(compiler_browse_btn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this,
            "Select LaTeX Compiler",
            compiler_command_edit_->text().trimmed().isEmpty() ? QString() : compiler_command_edit_->text().trimmed());
        if (!path.isEmpty()) {
            compiler_command_edit_->setText(path);
        }
    });
    auto *compiler_row = new QWidget(this);
    auto *compiler_row_layout = new QHBoxLayout(compiler_row);
    compiler_row_layout->setContentsMargins(0, 0, 0, 0);
    compiler_row_layout->setSpacing(6);
    compiler_row_layout->addWidget(compiler_command_edit_, 1);
    compiler_row_layout->addWidget(compiler_browse_btn, 0);

    grid_step_combo_ = new QComboBox(this);
    grid_step_combo_->addItem("10 mm", 10);
    grid_step_combo_->addItem("5 mm", 5);
    grid_step_combo_->addItem("2 mm", 2);
    grid_step_combo_->addItem("1 mm", 1);
    grid_step_combo_->addItem("0 (free)", 0);

    grid_extent_spin_ = new QSpinBox(this);
    grid_extent_spin_->setRange(20, 100);
    grid_extent_spin_->setSingleStep(5);
    grid_extent_spin_->setSuffix(" cm");
    grid_extent_spin_->setValue(20);

    theme_combo_ = new QComboBox(this);
    theme_combo_->addItem("System", "system");
    theme_combo_->addItem("Light", "light");
    theme_combo_->addItem("Dark", "dark");

    auto *form = new QFormLayout;
    form->addRow("Editor font", editor_font_family_combo_);
    form->addRow("Editor font size", editor_font_size_spin_);
    form->addRow(QString(), show_line_numbers_check_);
    form->addRow("Auto-compile delay", auto_compile_delay_spin_);
    form->addRow("LaTeX compiler", compiler_row);
    form->addRow("Grid/Snap step", grid_step_combo_);
    form->addRow("Grid extent", grid_extent_spin_);
    form->addRow("Look & Feel", theme_combo_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void settingsdialog::set_editor_font_size(int value) {
    editor_font_size_spin_->setValue(value);
}

int settingsdialog::editor_font_size() const {
    return editor_font_size_spin_->value();
}

void settingsdialog::set_editor_font_family(const QString &family) {
    if (family.trimmed().isEmpty()) {
        return;
    }
    editor_font_family_combo_->setCurrentFont(QFont(family));
}

QString settingsdialog::editor_font_family() const {
    return editor_font_family_combo_->currentFont().family();
}

void settingsdialog::set_show_line_numbers(bool enabled) {
    show_line_numbers_check_->setChecked(enabled);
}

bool settingsdialog::show_line_numbers() const {
    return show_line_numbers_check_->isChecked();
}

void settingsdialog::set_auto_compile_delay_ms(int value) {
    auto_compile_delay_spin_->setValue(value);
}

int settingsdialog::auto_compile_delay_ms() const {
    return auto_compile_delay_spin_->value();
}

void settingsdialog::set_compiler_command(const QString &command) {
    compiler_command_edit_->setText(command);
}

QString settingsdialog::compiler_command() const {
    return compiler_command_edit_->text().trimmed();
}

void settingsdialog::set_grid_step_mm(int value) {
    const int idx = grid_step_combo_->findData(value);
    grid_step_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

int settingsdialog::grid_step_mm() const {
    return grid_step_combo_->currentData().toInt();
}

void settingsdialog::set_grid_extent_cm(int value) {
    grid_extent_spin_->setValue(value);
}

int settingsdialog::grid_extent_cm() const {
    return grid_extent_spin_->value();
}

void settingsdialog::set_theme(const QString &theme_id) {
    const int idx = theme_combo_->findData(theme_id);
    theme_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString settingsdialog::theme() const {
    return theme_combo_->currentData().toString();
}
