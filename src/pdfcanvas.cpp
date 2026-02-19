#include "pdfcanvas.h"

#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <cmath>

pdfcanvas::pdfcanvas(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
}

void pdfcanvas::set_coordinates(const std::vector<coord_pair> &coords) {
    coordinates_ = coords;
    update();
}

void pdfcanvas::set_snap_mm(int mm) {
    snap_mm_ = qMax(0, mm);
}

bool pdfcanvas::load_pdf(const QString &pdf_path) {
    rendered_image_ = QImage();
    rendered_size_ = QSize();
    const QPdfDocument::Error err = pdf_document_.load(pdf_path);
    update();
    return err == QPdfDocument::Error::None;
}

void pdfcanvas::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor("#ffffff"));

    if (pdf_document_.status() != QPdfDocument::Status::Ready || pdf_document_.pageCount() <= 0) {
        painter.setPen(QColor("#666666"));
        painter.drawText(rect(), Qt::AlignCenter, "Compile to preview output");
        return;
    }

    const QSizeF page_size = pdf_document_.pagePointSize(0);
    if (page_size.width() <= 0 || page_size.height() <= 0) {
        return;
    }

    const double fit = 0.95 * qMin(width() / page_size.width(), height() / page_size.height());
    const double scale = fit * view_scale_;
    const int w = qMax(1, static_cast<int>(page_size.width() * scale));
    const int h = qMax(1, static_cast<int>(page_size.height() * scale));
    const QRect target_rect(static_cast<int>((width() - w) * 0.5 + pan_offset_.x()),
                            static_cast<int>((height() - h) * 0.5 + pan_offset_.y()),
                            w,
                            h);

    if (rendered_image_.isNull() || rendered_size_ != target_rect.size()) {
        rendered_image_ = pdf_document_.render(0, target_rect.size());
        rendered_size_ = target_rect.size();
    }

    if (!rendered_image_.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target_rect, rendered_image_);
    }

    update_calibration(target_rect);
    draw_coordinate_markers(painter);
}

void pdfcanvas::wheelEvent(QWheelEvent *event) {
    const QPoint delta = event->angleDelta();
    if (delta.y() == 0) {
        event->ignore();
        return;
    }

    const double steps = static_cast<double>(delta.y()) / 120.0;
    view_scale_ *= std::pow(1.12, steps);
    view_scale_ = qBound(0.2, view_scale_, 12.0);
    rendered_image_ = QImage();
    rendered_size_ = QSize();
    update();
    event->accept();
}

void pdfcanvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (calibration_valid_) {
            const int idx = hit_test_marker(event->position());
            if (idx >= 0) {
                marker_dragging_ = true;
                active_marker_index_ = idx;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
        }
        dragging_ = true;
        last_drag_pos_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void pdfcanvas::mouseMoveEvent(QMouseEvent *event) {
    if (marker_dragging_ && active_marker_index_ >= 0 && active_marker_index_ < static_cast<int>(coordinates_.size())) {
        QPointF world;
        if (screen_to_world(event->position(), world)) {
            if (snap_mm_ > 0) {
                const double step = static_cast<double>(snap_mm_) / 10.0;
                world.setX(std::round(world.x() / step) * step);
                world.setY(std::round(world.y() / step) * step);
            }
            coordinates_[active_marker_index_].x = world.x();
            coordinates_[active_marker_index_].y = world.y();
            update();
        }
        event->accept();
        return;
    }

    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF delta = event->position() - last_drag_pos_;
    pan_offset_ += delta;
    last_drag_pos_ = event->position();
    update();
    event->accept();
}

void pdfcanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && marker_dragging_) {
        marker_dragging_ = false;
        unsetCursor();
        if (active_marker_index_ >= 0 && active_marker_index_ < static_cast<int>(coordinates_.size())) {
            const coord_pair &c = coordinates_[active_marker_index_];
            emit coordinate_dragged(active_marker_index_, c.x, c.y);
        }
        active_marker_index_ = -1;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        unsetCursor();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

bool pdfcanvas::is_near_color(int r, int g, int b, int tr, int tg, int tb, int max_dist_sq) {
    const int dr = r - tr;
    const int dg = g - tg;
    const int db = b - tb;
    return (dr * dr + dg * dg + db * db) <= max_dist_sq;
}

bool pdfcanvas::find_color_centroid(const QImage &img, char target, QPointF &centroid_out) {
    if (img.isNull()) {
        return false;
    }

    int tr = 0;
    int tg = 0;
    int tb = 0;
    if (target == 'r') {
        tr = 253; tg = 17; tb = 251;
    } else if (target == 'g') {
        tr = 19; tg = 251; tb = 233;
    } else if (target == 'b') {
        tr = 241; tg = 251; tb = 17;
    }

    const int max_dist_sq = 55 * 55;
    double sx = 0.0;
    double sy = 0.0;
    int count = 0;

    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int r = qRed(row[x]);
            const int g = qGreen(row[x]);
            const int b = qBlue(row[x]);
            if (is_near_color(r, g, b, tr, tg, tb, max_dist_sq)) {
                sx += static_cast<double>(x);
                sy += static_cast<double>(y);
                ++count;
            }
        }
    }

    if (count < 1) {
        return false;
    }

    centroid_out = QPointF(sx / count, sy / count);
    return true;
}

void pdfcanvas::update_calibration(const QRect &target_rect) {
    calibration_valid_ = false;
    if (rendered_image_.isNull() || !target_rect.isValid()) {
        return;
    }

    QPointF red_local;
    QPointF green_local;
    QPointF blue_local;
    const bool ok_r = find_color_centroid(rendered_image_, 'r', red_local);
    const bool ok_g = find_color_centroid(rendered_image_, 'g', green_local);
    const bool ok_b = find_color_centroid(rendered_image_, 'b', blue_local);
    if (!ok_r || !ok_g || !ok_b) {
        return;
    }

    const QPointF top_left = target_rect.topLeft();
    origin_px_ = top_left + red_local;
    axis_x_px_ = top_left + green_local;
    axis_y_px_ = top_left + blue_local;

    const QPointF u = axis_x_px_ - origin_px_;
    const QPointF v = axis_y_px_ - origin_px_;
    const double det = u.x() * v.y() - u.y() * v.x();
    calibration_valid_ = std::abs(det) > 1e-6;
}

QPointF pdfcanvas::world_to_screen(double x, double y) const {
    const QPointF u = axis_x_px_ - origin_px_;
    const QPointF v = axis_y_px_ - origin_px_;
    return origin_px_ + u * x + v * y;
}

bool pdfcanvas::screen_to_world(const QPointF &p, QPointF &world_out) const {
    if (!calibration_valid_) {
        return false;
    }

    const QPointF u = axis_x_px_ - origin_px_;
    const QPointF v = axis_y_px_ - origin_px_;
    const double det = u.x() * v.y() - u.y() * v.x();
    if (std::abs(det) < 1e-9) {
        return false;
    }

    const QPointF d = p - origin_px_;
    const double x = (d.x() * v.y() - d.y() * v.x()) / det;
    const double y = (u.x() * d.y() - u.y() * d.x()) / det;
    world_out = QPointF(x, y);
    return true;
}

int pdfcanvas::hit_test_marker(const QPointF &pos) const {
    constexpr double threshold = 10.0;
    for (int i = 0; i < static_cast<int>(coordinates_.size()); ++i) {
        const QPointF p = world_to_screen(coordinates_[i].x, coordinates_[i].y);
        if (QLineF(pos, p).length() <= threshold) {
            return i;
        }
    }
    return -1;
}

void pdfcanvas::draw_coordinate_markers(QPainter &painter) {
    if (!calibration_valid_ || coordinates_.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#dc2626"), 2.0));
    constexpr int half = 6;
    for (const coord_pair &c : coordinates_) {
        const QPointF p = world_to_screen(c.x, c.y);
        painter.drawLine(QPointF(p.x() - half, p.y()), QPointF(p.x() + half, p.y()));
        painter.drawLine(QPointF(p.x(), p.y() - half), QPointF(p.x(), p.y() + half));
    }
    painter.restore();
}
