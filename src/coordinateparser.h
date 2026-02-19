#ifndef COORDINATEPARSER_H
#define COORDINATEPARSER_H

#include <QString>
#include <vector>

#include "model.h"

namespace coordinateparser {

std::vector<coord_ref> extract_refs(const QString &source);
std::vector<coord_pair> extract_pairs(const QString &source);
QString format_number(double value);

} // namespace coordinateparser

#endif
