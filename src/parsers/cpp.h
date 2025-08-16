#ifndef __AFC_EDITOR_CPP_PARSE_TREE_H__
#define __AFC_EDITOR_CPP_PARSE_TREE_H__

#include <memory>
#include <unordered_set>

#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/parse_tree.h"

namespace afc::editor::parsers {

language::NonNull<std::unique_ptr<TreeParser>> NewCppTreeParser(
    ParserId parser_id,
    std::unordered_set<language::lazy_string::NonEmptySingleLine> keywords,
    std::unordered_set<language::lazy_string::NonEmptySingleLine> typos,
    IdentifierBehavior identifier_behavior);

}  // namespace afc::editor::parsers
#endif  // __AFC_EDITOR_CPP_PARSE_TREE_H__
