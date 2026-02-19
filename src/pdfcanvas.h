#ifndef PDFCANVAS_H
#define PDFCANVAS_H

#include <QImage>
#include <QPdfDocument>
#include <QPointF>
#include <QSize>
#include <QWidget>
#include <vector>

#include "model.h"

class pdfcanvas : public QWidget {
    Q_OBJECT

public:
    explicit pdfcanvas(QWidget *parent = nullptr);

    void set_coordinates(const std::vector<coord_pair> &coords);
    void set_snap_mm(int mm);
    bool load_pdf(const QString &pdf_path);

signals:
    void coordinate_dragged(int index, double x, double y);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    static bool is_near_color(int r, int g, int b, int tr, int tg, int tb, int max_dist_sq);
    static bool find_color_centroid(const QImage &img, char target, QPointF &centroid_out);

    void update_calibration(const QRect &target_rect);
    QPointF world_to_screen(double x, double y) const;
    bool screen_to_world(const QPointF &p, QPointF &world_out) const;
    int hit_test_marker(const QPointF &pos) const;
    void draw_coordinate_markers(QPainter &painter);

    QPdfDocument pdf_document_;
    QImage rendered_image_;
    QSize rendered_size_;
    double view_scale_ = 1.0;
    QPointF pan_offset_{0.0, 0.0};
    bool dragging_ = false;
    QPointF last_drag_pos_{0.0, 0.0};
    bool marker_dragging_ = false;
    int active_marker_index_ = -1;
    std::vector<coord_pair> coordinates_;
    bool calibration_valid_ = false;
    QPointF origin_px_{0.0, 0.0};
    QPointF axis_x_px_{1.0, 0.0};
    QPointF axis_y_px_{0.0, -1.0};
    int snap_mm_ = 10;
};

#endif
