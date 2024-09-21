#ifndef __AFC_EDITOR_PARSE_TREE_H__
#define __AFC_EDITOR_PARSE_TREE_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/url.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"

namespace afc::editor {
enum class IdentifierBehavior { kNone, kColorByHash };

class ParserId
    : public language::GhostType<ParserId,
                                 language::lazy_string::NonEmptySingleLine> {
 public:
  using GhostType::GhostType;

  static const ParserId& Text();
  static const ParserId& Cpp();
  static const ParserId& Diff();
  static const ParserId& Markdown();
  static const ParserId& Csv();
};

class ParseTreeProperty
    : public language::GhostType<ParseTreeProperty,
                                 language::lazy_string::NonEmptySingleLine> {
  using GhostType::GhostType;

 public:
  static const ParseTreeProperty& Link();
  static const ParseTreeProperty& LinkTarget();

  static const ParseTreeProperty& TableCell(size_t id);
  static const ParseTreeProperty& CellContent();

  static const ParseTreeProperty& StringValue();
  static const ParseTreeProperty& NumberValue();
};

class ParseTree {
 public:
  // The empty route just means "stop at the root". Otherwise, it means to go
  // down to the Nth children at each step N.
  using Route = std::vector<size_t>;

  ParseTree() = default;

  ParseTree(language::text::Range range);
  ParseTree(const ParseTree& other);

  language::text::Range range() const;
  void set_range(language::text::Range range);

  size_t depth() const;

  const infrastructure::screen::LineModifierSet& modifiers() const;
  void set_modifiers(infrastructure::screen::LineModifierSet modifiers);
  void InsertModifier(infrastructure::screen::LineModifier modifier);

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

  language::text::Range range_;
  size_t depth_ = 0;
  infrastructure::screen::LineModifierSet modifiers_;
  std::unordered_set<ParseTreeProperty> properties_;
};

// Returns a copy of tree that only includes children that cross line
// boundaries. This is useful to reduce the noise shown in the tree.
ParseTree SimplifyTree(const ParseTree& tree);

// Produces simplified (by SimplifyTree) copy of a simplified tree, where lines
// are remapped from an input of `input_lines` lines to an output of exactly
// `output_lines`.
ParseTree ZoomOutTree(const ParseTree& input,
                      language::text::LineNumberDelta input_lines,
                      language::text::LineNumberDelta output_lines);

// Find the route down a given parse tree always selecting the first children
// that ends after the current position. The children selected at each step may
// not include the position (it may start after the position).
ParseTree::Route FindRouteToPosition(
    const ParseTree& root, const language::text::LineColumn& position);

std::vector<const ParseTree*> MapRoute(const ParseTree& root,
                                       const ParseTree::Route& route);

const ParseTree& FollowRoute(const ParseTree& root,
                             const ParseTree::Route& route);

std::ostream& operator<<(std::ostream& os, const ParseTree& lc);

class TreeParser {
 public:
  virtual ~TreeParser() = default;

  static bool IsNull(TreeParser*);

  virtual ParseTree FindChildren(const language::text::LineSequence& lines,
                                 language::text::Range range) = 0;
};

language::NonNull<std::unique_ptr<TreeParser>> NewNullTreeParser();
language::NonNull<std::unique_ptr<TreeParser>> NewCharTreeParser();
language::NonNull<std::unique_ptr<TreeParser>> NewWordsTreeParser(
    language::lazy_string::LazyString word_characters,
    std::unordered_set<language::lazy_string::NonEmptySingleLine> typos,
    language::NonNull<std::unique_ptr<TreeParser>> delegate);
language::NonNull<std::unique_ptr<TreeParser>> NewLineTreeParser(
    language::NonNull<std::unique_ptr<TreeParser>> delegate);

void RegisterParseTreeFunctions(language::gc::Pool& pool,
                                vm::Environment& environment);

// Returns the URL that can be extracted from the current tree.
language::ValueOrError<URL> FindLinkTarget(
    const ParseTree& tree, const language::text::LineSequence& contents);
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<
    language::NonNull<std::shared_ptr<const editor::ParseTree>>> {
  static language::NonNull<std::shared_ptr<const editor::ParseTree>> get(
      Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool,
      language::NonNull<std::shared_ptr<const editor::ParseTree>> value);
  static const types::ObjectName object_type_name;
};
}  // namespace afc::vm

#endif  // __AFC_EDITOR_PARSE_TREE_H__
