#ifndef __AFC_EDITOR_CPP_PARSE_TREE_H__
#define __AFC_EDITOR_CPP_PARSE_TREE_H__

#include <memory>
#include <unordered_set>

#include "src/language/safe_types.h"
#include "src/parse_tree.h"

namespace afc::editor {
language::NonNull<std::unique_ptr<TreeParser>> NewCppTreeParser(
    std::unordered_set<std::wstring> keywords,
    std::unordered_set<std::wstring> typos,
    IdentifierBehavior identifier_behavior);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_CPP_PARSE_TREE_H__
