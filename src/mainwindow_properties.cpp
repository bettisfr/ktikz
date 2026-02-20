#include "mainwindow.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStatusBar>

#include <algorithm>
#include <tuple>
#include <vector>

#include "compileservice.h"
#include "coordinateparser.h"
#include "pdfcanvas.h"

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
