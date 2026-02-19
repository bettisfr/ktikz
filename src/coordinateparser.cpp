#include "coordinateparser.h"

#include <QRegularExpression>

namespace {

const QRegularExpression &coord_pattern() {
    static const QRegularExpression pattern(
        R"(\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\))");
    return pattern;
}

const QRegularExpression &circle_pattern() {
    static const QRegularExpression pattern(
        R"(\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\)\s*circle\s*\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\))");
    return pattern;
}

const QRegularExpression &ellipse_pattern() {
    static const QRegularExpression pattern(
        R"(\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\)\s*ellipse\s*\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*and\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\))");
    return pattern;
}

const QRegularExpression &bezier_pattern() {
    static const QRegularExpression pattern(
        R"((?:\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\)\s*)?\.\.\s*controls\s*\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\)\s*and\s*\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\)\s*\.\.\s*\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\))");
    return pattern;
}

const QRegularExpression &rectangle_pattern() {
    static const QRegularExpression pattern(
        R"(\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\)\s*rectangle\s*\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\))");
    return pattern;
}

} // namespace

namespace coordinateparser {

std::vector<coord_ref> extract_refs(const QString &source) {
    std::vector<coord_ref> refs;
    QRegularExpressionMatchIterator it = coord_pattern().globalMatch(source);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok_x = false;
        bool ok_y = false;
        const double x = m.captured(1).toDouble(&ok_x);
        const double y = m.captured(2).toDouble(&ok_y);
        if (!ok_x || !ok_y) {
            continue;
        }

        coord_ref ref;
        ref.start = m.capturedStart(0);
        ref.end = ref.start + m.capturedLength(0);
        ref.x = x;
        ref.y = y;
        refs.push_back(ref);
    }
    return refs;
}

std::vector<coord_pair> extract_pairs(const QString &source) {
    std::vector<coord_pair> pairs;
    const std::vector<coord_ref> refs = extract_refs(source);
    pairs.reserve(refs.size());
    for (const coord_ref &ref : refs) {
        pairs.push_back({ref.x, ref.y});
    }
    return pairs;
}

std::vector<circle_ref> extract_circle_refs(const QString &source) {
    std::vector<circle_ref> refs;
    QRegularExpressionMatchIterator it = circle_pattern().globalMatch(source);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok_cx = false;
        bool ok_cy = false;
        bool ok_r = false;
        const double cx = m.captured(1).toDouble(&ok_cx);
        const double cy = m.captured(2).toDouble(&ok_cy);
        const double r = m.captured(3).toDouble(&ok_r);
        if (!ok_cx || !ok_cy || !ok_r) {
            continue;
        }

        circle_ref ref;
        ref.radius_start = m.capturedStart(3);
        ref.radius_end = ref.radius_start + m.capturedLength(3);
        ref.cx = cx;
        ref.cy = cy;
        ref.r = r;
        refs.push_back(ref);
    }
    return refs;
}

std::vector<circle_pair> extract_circle_pairs(const QString &source) {
    std::vector<circle_pair> pairs;
    const std::vector<circle_ref> refs = extract_circle_refs(source);
    pairs.reserve(refs.size());
    for (const circle_ref &ref : refs) {
        pairs.push_back({ref.cx, ref.cy, ref.r});
    }
    return pairs;
}

std::vector<ellipse_ref> extract_ellipse_refs(const QString &source) {
    std::vector<ellipse_ref> refs;
    QRegularExpressionMatchIterator it = ellipse_pattern().globalMatch(source);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok_cx = false;
        bool ok_cy = false;
        bool ok_rx = false;
        bool ok_ry = false;
        const double cx = m.captured(1).toDouble(&ok_cx);
        const double cy = m.captured(2).toDouble(&ok_cy);
        const double rx = m.captured(3).toDouble(&ok_rx);
        const double ry = m.captured(4).toDouble(&ok_ry);
        if (!ok_cx || !ok_cy || !ok_rx || !ok_ry) {
            continue;
        }

        ellipse_ref ref;
        ref.rx_start = m.capturedStart(3);
        ref.rx_end = ref.rx_start + m.capturedLength(3);
        ref.ry_start = m.capturedStart(4);
        ref.ry_end = ref.ry_start + m.capturedLength(4);
        ref.cx = cx;
        ref.cy = cy;
        ref.rx = rx;
        ref.ry = ry;
        refs.push_back(ref);
    }
    return refs;
}

std::vector<ellipse_pair> extract_ellipse_pairs(const QString &source) {
    std::vector<ellipse_pair> pairs;
    const std::vector<ellipse_ref> refs = extract_ellipse_refs(source);
    pairs.reserve(refs.size());
    for (const ellipse_ref &ref : refs) {
        pairs.push_back({ref.cx, ref.cy, ref.rx, ref.ry});
    }
    return pairs;
}

std::vector<bezier_ref> extract_bezier_refs(const QString &source) {
    std::vector<bezier_ref> refs;
    QRegularExpressionMatchIterator it = bezier_pattern().globalMatch(source);
    bool have_prev_end = false;
    double prev_x3 = 0.0;
    double prev_y3 = 0.0;
    int last_seg_end = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok_x0 = false;
        bool ok_y0 = false;
        bool ok_x1 = false;
        bool ok_y1 = false;
        bool ok_x2 = false;
        bool ok_y2 = false;
        bool ok_x3 = false;
        bool ok_y3 = false;
        const int seg_start = m.capturedStart(0);
        if (seg_start > last_seg_end) {
            const QString between = source.mid(last_seg_end, seg_start - last_seg_end);
            if (between.contains(';')) {
                have_prev_end = false;
            }
        }

        const bool has_explicit_start = m.capturedStart(1) >= 0 && m.capturedStart(2) >= 0;
        double x0 = 0.0;
        double y0 = 0.0;
        if (has_explicit_start) {
            x0 = m.captured(1).toDouble(&ok_x0);
            y0 = m.captured(2).toDouble(&ok_y0);
        } else if (have_prev_end) {
            ok_x0 = true;
            ok_y0 = true;
            x0 = prev_x3;
            y0 = prev_y3;
        }

        const double x1 = m.captured(3).toDouble(&ok_x1);
        const double y1 = m.captured(4).toDouble(&ok_y1);
        const double x2 = m.captured(5).toDouble(&ok_x2);
        const double y2 = m.captured(6).toDouble(&ok_y2);
        const double x3 = m.captured(7).toDouble(&ok_x3);
        const double y3 = m.captured(8).toDouble(&ok_y3);
        if (!ok_x0 || !ok_y0 || !ok_x1 || !ok_y1 || !ok_x2 || !ok_y2 || !ok_x3 || !ok_y3) {
            last_seg_end = m.capturedEnd(0);
            continue;
        }

        bezier_ref ref;
        ref.x1_start = m.capturedStart(3);
        ref.x1_end = ref.x1_start + m.capturedLength(3);
        ref.y1_start = m.capturedStart(4);
        ref.y1_end = ref.y1_start + m.capturedLength(4);
        ref.x2_start = m.capturedStart(5);
        ref.x2_end = ref.x2_start + m.capturedLength(5);
        ref.y2_start = m.capturedStart(6);
        ref.y2_end = ref.y2_start + m.capturedLength(6);
        ref.x0 = x0;
        ref.y0 = y0;
        ref.x1 = x1;
        ref.y1 = y1;
        ref.x2 = x2;
        ref.y2 = y2;
        ref.x3 = x3;
        ref.y3 = y3;
        refs.push_back(ref);

        have_prev_end = true;
        prev_x3 = x3;
        prev_y3 = y3;
        last_seg_end = m.capturedEnd(0);
    }
    return refs;
}

std::vector<bezier_pair> extract_bezier_pairs(const QString &source) {
    std::vector<bezier_pair> pairs;
    const std::vector<bezier_ref> refs = extract_bezier_refs(source);
    pairs.reserve(refs.size());
    for (const bezier_ref &ref : refs) {
        pairs.push_back({ref.x0, ref.y0, ref.x1, ref.y1, ref.x2, ref.y2, ref.x3, ref.y3});
    }
    return pairs;
}

std::vector<rectangle_ref> extract_rectangle_refs(const QString &source) {
    std::vector<rectangle_ref> refs;
    QRegularExpressionMatchIterator it = rectangle_pattern().globalMatch(source);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok_x1 = false;
        bool ok_y1 = false;
        bool ok_x2 = false;
        bool ok_y2 = false;
        const double x1 = m.captured(1).toDouble(&ok_x1);
        const double y1 = m.captured(2).toDouble(&ok_y1);
        const double x2 = m.captured(3).toDouble(&ok_x2);
        const double y2 = m.captured(4).toDouble(&ok_y2);
        if (!ok_x1 || !ok_y1 || !ok_x2 || !ok_y2) {
            continue;
        }

        rectangle_ref ref;
        ref.x2_start = m.capturedStart(3);
        ref.x2_end = ref.x2_start + m.capturedLength(3);
        ref.y2_start = m.capturedStart(4);
        ref.y2_end = ref.y2_start + m.capturedLength(4);
        ref.x1 = x1;
        ref.y1 = y1;
        ref.x2 = x2;
        ref.y2 = y2;
        refs.push_back(ref);
    }
    return refs;
}

std::vector<rectangle_pair> extract_rectangle_pairs(const QString &source) {
    std::vector<rectangle_pair> pairs;
    const std::vector<rectangle_ref> refs = extract_rectangle_refs(source);
    pairs.reserve(refs.size());
    for (const rectangle_ref &ref : refs) {
        pairs.push_back({ref.x1, ref.y1, ref.x2, ref.y2});
    }
    return pairs;
}

QString format_number(double value) {
    QString s = QString::number(value, 'f', 4);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) {
        s.chop(1);
    }
    if (s == "-0") {
        s = "0";
    }
    return s;
}

} // namespace coordinateparser
