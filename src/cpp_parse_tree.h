#ifndef __AFC_EDITOR_CPP_PARSE_TREE_H__
#define __AFC_EDITOR_CPP_PARSE_TREE_H__

#include <memory>
#include <unordered_set>

#include "src/parse_tree.h"

namespace afc {
namespace editor {

enum class IdentifierBehavior { kNone, kColorByHash };

std::unique_ptr<TreeParser> NewCppTreeParser(
    std::unordered_set<std::wstring> keywords,
    std::unordered_set<std::wstring> typos,
    IdentifierBehavior identifier_behavior);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_CPP_PARSE_TREE_H__
