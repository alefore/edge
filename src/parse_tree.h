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
  // The empty route just means "stop at the root". Otherwise, it means to go
  // down to the Nth children at each step N.
  using Route = std::vector<size_t>;

  ParseTree() = default;

  ParseTree(const ParseTree& other)
      : range(other.range),
        modifiers(other.modifiers),
        children() {
    children = other.children;
  }

  Range range;
  std::unordered_set<LineModifier, hash<int>> modifiers;
  Tree<ParseTree> children;
  size_t depth = 0;
};

// Inserts a new child into a tree and returns a pointer to it.
//
// Unlike the usual unique_ptr uses, ownership of the child remains with the
// parent. However, the custom deleter adjusts the depth in the parent once
// the child goes out of scope. The standard use is that changes to the child
// will be done through the returned unique_ptr, so that these changes are
// taken into account to adjust the depth of the parent.
std::unique_ptr<ParseTree, std::function<void(ParseTree*)>>
PushChild(ParseTree* parent);

// Returns a copy of tree that only includes children that cross line
// boundaries. This is useful to reduce the noise shown in the tree.
void SimplifyTree(const ParseTree& tree, ParseTree* output);

// Find the route down a given parse tree always selecting the first children
// that ends after the current position. The children selected at each step may
// not include the position (it may start after the position).
ParseTree::Route FindRouteToPosition(
    const ParseTree& root, const LineColumn& position);

std::vector<const ParseTree*> MapRoute(
    const ParseTree& root, const ParseTree::Route& route);

const ParseTree* FollowRoute(const ParseTree& root,
                             const ParseTree::Route& route);

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
