#ifndef __AFC_EDITOR_PARSERS_MARKDOWN_H__
#define __AFC_EDITOR_PARSERS_MARKDOWN_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/parse_tree.h"

namespace afc {
namespace editor {
namespace parsers {
language::NonNull<std::unique_ptr<TreeParser>> NewMarkdownTreeParser();
}  // namespace parsers
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_PARSERS_MARKDOWN_H__
