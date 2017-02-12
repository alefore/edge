#ifndef __AFC_EDITOR_CPP_PARSE_TREE_H__
#define __AFC_EDITOR_CPP_PARSE_TREE_H__

#include <memory>

#include "parse_tree.h"

namespace afc {
namespace editor {

std::unique_ptr<TreeParser> NewCppTreeParser();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_CPP_PARSE_TREE_H__
