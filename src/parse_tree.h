#ifndef __AFC_EDITOR_PARSE_TREE_H__
#define __AFC_EDITOR_PARSE_TREE_H__

#include <memory>
#include <string>
#include <unordered_set>

#include "line.h"
#include "line_column.h"
#include "tree.h"

namespace afc {
namespace editor {

struct ParseTree {
  LineColumn begin;
  LineColumn end;
  std::unordered_set<Line::Modifier, hash<int>> modifiers;
  Tree<ParseTree> children;
};

std::ostream& operator<<(std::ostream& os, const ParseTree& lc);

class OpenBuffer;

class TreeParser {
 public:
  // Removes all children from root and re-scans it (from begin to end).
  virtual void FindChildren(const OpenBuffer& buffer, ParseTree* root) = 0;
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
