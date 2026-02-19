#include "coordinateparser.h"

#include <QRegularExpression>

namespace {

const QRegularExpression &coord_pattern() {
    static const QRegularExpression pattern(
        R"(\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\))");
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
