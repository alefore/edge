#ifndef __AFC_VM_FILTER_SIMILAR_NAMES_H__
#define __AFC_VM_FILTER_SIMILAR_NAMES_H__

#include <vector>

#include "src/vm/types.h"

namespace afc::vm {
std::vector<Identifier> FilterSimilarNames(Identifier name,
                                           std::vector<Identifier> candidates);
}

#endif  // __AFC_VM_FILTER_SIMILAR_NAMES_H__
