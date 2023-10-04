#ifndef __AFC_VM_FILTER_SIMILAR_NAMES_H__
#define __AFC_VM_FILTER_SIMILAR_NAMES_H__

#include <string>
#include <vector>

namespace afc::vm {
std::vector<std::wstring> FilterSimilarNames(
    std::wstring name, std::vector<std::wstring> candidates);
}

#endif  // __AFC_VM_FILTER_SIMILAR_NAMES_H__
