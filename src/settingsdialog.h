#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QComboBox;
class QCheckBox;
class QFontComboBox;
class QLineEdit;
class QSpinBox;

class settingsdialog : public QDialog {
    Q_OBJECT

public:
    explicit settingsdialog(QWidget *parent = nullptr);

    void set_editor_font_size(int value);
    int editor_font_size() const;
    void set_editor_font_family(const QString &family);
    QString editor_font_family() const;
    void set_show_line_numbers(bool enabled);
    bool show_line_numbers() const;

    void set_auto_compile_delay_ms(int value);
    int auto_compile_delay_ms() const;
    void set_compiler_command(const QString &command);
    QString compiler_command() const;

    void set_grid_step_mm(int value);
    int grid_step_mm() const;

    void set_grid_extent_cm(int value);
    int grid_extent_cm() const;
    void set_theme(const QString &theme_id);
    QString theme() const;

private:
    QFontComboBox *editor_font_family_combo_ = nullptr;
    QSpinBox *editor_font_size_spin_ = nullptr;
    QCheckBox *show_line_numbers_check_ = nullptr;
    QSpinBox *auto_compile_delay_spin_ = nullptr;
    QLineEdit *compiler_command_edit_ = nullptr;
    QComboBox *grid_step_combo_ = nullptr;
    QSpinBox *grid_extent_spin_ = nullptr;
    QComboBox *theme_combo_ = nullptr;
};

#endif
