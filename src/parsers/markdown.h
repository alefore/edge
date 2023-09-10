#ifndef __AFC_EDITOR_PARSERS_MARKDOWN_H__
#define __AFC_EDITOR_PARSERS_MARKDOWN_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/language/text/line_sequence.h"
#include "src/parse_tree.h"

namespace afc {
namespace editor {
namespace parsers {
language::NonNull<std::unique_ptr<TreeParser>> NewMarkdownTreeParser(
    std::wstring symbol_characters,
    std::unique_ptr<const language::text::MutableLineSequence> dictionary);
}  // namespace parsers
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_PARSERS_MARKDOWN_H__
