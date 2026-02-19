#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KMainWindow>
#include <vector>

#include "model.h"

class QPlainTextEdit;
class QComboBox;
class QSpinBox;

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
    void on_circle_radius_dragged(int index, double radius);
    void on_ellipse_radii_dragged(int index, double rx, double ry);
    void on_bezier_control_dragged(int index, int control_idx, double x, double y);
    void on_rectangle_corner_dragged(int index, double x2, double y2);
    void on_grid_step_changed(int value);
    void on_grid_extent_changed(int value);

private:
    void create_menu_and_toolbar();

    KTextEditor::Document *editor_doc_ = nullptr;
    KTextEditor::View *editor_view_ = nullptr;
    pdfcanvas *preview_canvas_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QComboBox *grid_step_combo_ = nullptr;
    QSpinBox *grid_extent_spin_ = nullptr;
    compileservice *compile_service_ = nullptr;

    std::vector<coord_ref> coordinate_refs_;
    std::vector<circle_ref> circle_refs_;
    std::vector<ellipse_ref> ellipse_refs_;
    std::vector<bezier_ref> bezier_refs_;
    std::vector<rectangle_ref> rectangle_refs_;
    int grid_snap_mm_ = 10;
    int grid_display_mm_ = 10;
    int grid_extent_cm_ = 20;
    QString current_file_path_;
};

#endif
