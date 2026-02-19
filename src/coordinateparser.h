#ifndef COORDINATEPARSER_H
#define COORDINATEPARSER_H

#include <QString>
#include <vector>

#include "model.h"

namespace coordinateparser {

std::vector<coord_ref> extract_refs(const QString &source);
std::vector<coord_pair> extract_pairs(const QString &source);
std::vector<circle_ref> extract_circle_refs(const QString &source);
std::vector<circle_pair> extract_circle_pairs(const QString &source);
std::vector<ellipse_ref> extract_ellipse_refs(const QString &source);
std::vector<ellipse_pair> extract_ellipse_pairs(const QString &source);
std::vector<bezier_ref> extract_bezier_refs(const QString &source);
std::vector<bezier_pair> extract_bezier_pairs(const QString &source);
std::vector<rectangle_ref> extract_rectangle_refs(const QString &source);
std::vector<rectangle_pair> extract_rectangle_pairs(const QString &source);
QString format_number(double value);

} // namespace coordinateparser

#endif
