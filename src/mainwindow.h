#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KMainWindow>
#include <vector>

#include "model.h"

class QComboBox;
class QPlainTextEdit;

namespace KTextEditor {
class Document;
class View;
}

class compileservice;
class pdfcanvas;

class mainwindow : public KMainWindow {
    Q_OBJECT

public:
    explicit mainwindow(QWidget *parent = nullptr);

private slots:
    void load_file();
    void compile();
    void on_compile_service_output(const QString &text);
    void on_compile_finished(bool success, const QString &pdf_path, const QString &message);
    void on_coordinate_dragged(int index, double x, double y);
    void on_grid_step_changed(int index);

private:
    void create_menu_and_toolbar();

    KTextEditor::Document *editor_doc_ = nullptr;
    KTextEditor::View *editor_view_ = nullptr;
    pdfcanvas *preview_canvas_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QComboBox *grid_step_combo_ = nullptr;
    compileservice *compile_service_ = nullptr;

    std::vector<coord_ref> coordinate_refs_;
    int grid_snap_mm_ = 10;
    int grid_display_mm_ = 10;
    QString current_file_path_;
};

#endif
