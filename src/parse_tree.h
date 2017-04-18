#ifndef __AFC_EDITOR_PARSE_TREE_H__
#define __AFC_EDITOR_PARSE_TREE_H__

#include <memory>
#include <string>
#include <unordered_set>

#include "src/buffer_contents.h"
#include "line.h"
#include "line_column.h"
#include "tree.h"

namespace afc {
namespace editor {

struct ParseTree {
  ParseTree() = default;

  ParseTree(const ParseTree& other)
      : range(other.range),
        modifiers(other.modifiers),
        children() {
    children = other.children;
  }

  Range range;
  std::unordered_set<Line::Modifier, hash<int>> modifiers;
  Tree<ParseTree> children;
};

std::ostream& operator<<(std::ostream& os, const ParseTree& lc);

class OpenBuffer;

class TreeParser {
 public:
  static bool IsNull(TreeParser*);

  // Removes all children from root and re-scans it (from begin to end).
  virtual void FindChildren(const BufferContents& lines, ParseTree* root) = 0;
};

std::unique_ptr<TreeParser> NewNullTreeParser();
std::unique_ptr<TreeParser> NewCharTreeParser();
std::unique_ptr<TreeParser> NewWordsTreeParser(
    std::wstring word_characters,
    std::unique_ptr<TreeParser> delegate);
std::unique_ptr<TreeParser> NewLineTreeParser(
    std::unique_ptr<TreeParser> delegate);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_PARSE_TREE_H__
