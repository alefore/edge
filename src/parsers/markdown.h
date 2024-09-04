#ifndef __AFC_EDITOR_PARSERS_MARKDOWN_H__
#define __AFC_EDITOR_PARSERS_MARKDOWN_H__

#include <memory>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/sorted_line_sequence.h"
#include "src/parse_tree.h"

namespace afc::editor::parsers {
language::NonNull<std::unique_ptr<TreeParser>> NewMarkdownTreeParser(
    language::lazy_string::LazyString symbol_characters,
    language::text::SortedLineSequence dictionary);
}  // namespace afc::editor::parsers

#endif  // __AFC_EDITOR_PARSERS_MARKDOWN_H__
