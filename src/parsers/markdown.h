#ifndef __AFC_EDITOR_PARSERS_MARKDOWN_H__
#define __AFC_EDITOR_PARSERS_MARKDOWN_H__

#include <memory>

#include "src/parse_tree.h"

namespace afc {
namespace editor {
namespace parsers {
std::unique_ptr<TreeParser> NewMarkdownTreeParser();
}  // namespace parsers
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_PARSERS_MARKDOWN_H__
