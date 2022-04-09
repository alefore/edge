#ifndef __AFC_EDITOR_PARSE_TREE_H__
#define __AFC_EDITOR_PARSE_TREE_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "src/buffer_contents.h"
#include "src/ghost_type.h"
#include "src/line_column.h"
#include "src/line_modifier.h"

namespace afc::editor {
class BufferContents;

class ParseTreeProperty {
 public:
  static const ParseTreeProperty& Link();
  static const ParseTreeProperty& LinkTarget();

  GHOST_TYPE_EQ(ParseTreeProperty, value);

 private:
  GHOST_TYPE_CONSTRUCTOR(ParseTreeProperty, std::wstring, value);
  GHOST_TYPE_HASH_FRIEND(afc::editor::ParseTreeProperty, value);
  const std::wstring& read() const { return value; }

  std::wstring value;
};
}  // namespace afc::editor

GHOST_TYPE_HASH(afc::editor::ParseTreeProperty);

namespace afc::editor {
class ParseTree {
 public:
  // The empty route just means "stop at the root". Otherwise, it means to go
  // down to the Nth children at each step N.
  using Route = std::vector<size_t>;

  ParseTree() = default;

  ParseTree(Range range);
  ParseTree(const ParseTree& other);

  Range range() const;
  void set_range(Range range);

  size_t depth() const;

  const LineModifierSet& modifiers() const;
  void set_modifiers(LineModifierSet modifiers);
  void InsertModifier(LineModifier modifier);

  const std::vector<ParseTree>& children() const;

  // Unlike the usual unique_ptr uses, ownership of the child remains with the
  // parent. However, the custom deleter adjusts the depth in the parent once
  // the child goes out of scope. The standard use is that changes to the child
  // will be done through the returned unique_ptr, so that these changes are
  // taken into account to adjust the depth of the parent.
  std::unique_ptr<ParseTree, std::function<void(ParseTree*)>> MutableChildren(
      size_t i);

  void Reset();

  void PushChild(ParseTree child);

  size_t hash() const;

  void set_properties(std::unordered_set<ParseTreeProperty> properties);
  const std::unordered_set<ParseTreeProperty>& properties() const;

 private:
  void XorChildHash(size_t position);

  std::vector<ParseTree> children_;

  // The xor of the hashes of all children (including their positions).
  size_t children_hashes_ = 0;

  Range range_;
  size_t depth_ = 0;
  LineModifierSet modifiers_;
  std::unordered_set<ParseTreeProperty> properties_;
};

// Returns a copy of tree that only includes children that cross line
// boundaries. This is useful to reduce the noise shown in the tree.
ParseTree SimplifyTree(const ParseTree& tree);

// Produces simplified (by SimplifyTree) copy of a simplified tree, where lines
// are remapped from an input of `input_lines` lines to an output of exactly
// `output_lines`.
ParseTree ZoomOutTree(const ParseTree& input, LineNumberDelta input_lines,
                      LineNumberDelta output_lines);

// Find the route down a given parse tree always selecting the first children
// that ends after the current position. The children selected at each step may
// not include the position (it may start after the position).
ParseTree::Route FindRouteToPosition(const ParseTree& root,
                                     const LineColumn& position);

std::vector<const ParseTree*> MapRoute(const ParseTree& root,
                                       const ParseTree::Route& route);

const ParseTree* FollowRoute(const ParseTree& root,
                             const ParseTree::Route& route);

std::ostream& operator<<(std::ostream& os, const ParseTree& lc);

class TreeParser {
 public:
  static bool IsNull(TreeParser*);

  virtual ParseTree FindChildren(const BufferContents& lines, Range range) = 0;
};

std::unique_ptr<TreeParser> NewNullTreeParser();
std::unique_ptr<TreeParser> NewCharTreeParser();
std::unique_ptr<TreeParser> NewWordsTreeParser(
    std::wstring word_characters, std::unordered_set<std::wstring> typos,
    std::unique_ptr<TreeParser> delegate);
std::unique_ptr<TreeParser> NewLineTreeParser(
    std::unique_ptr<TreeParser> delegate);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_PARSE_TREE_H__
