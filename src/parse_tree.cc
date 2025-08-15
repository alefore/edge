#include "src/parse_tree.h"

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/language/hash.h"
#include "src/language/text/line_column_vm.h"
#include "src/seek.h"
#include "src/url.h"
#include "src/vm/container.h"
#include "src/vm/environment.h"

namespace container = afc::language::container;

using afc::concurrent::MakeProtected;
using afc::concurrent::Protected;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::compute_hash;
using afc::language::Error;
using afc::language::hash_combine;
using afc::language::MakeHashableIteratorRange;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineRange;
using afc::language::text::LineSequence;
using afc::language::text::Range;
using afc::vm::Identifier;
using afc::vm::kPurityTypeReader;

namespace afc::vm {
template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<Protected<
    std::vector<NonNull<std::shared_ptr<const editor::ParseTree>>>>>>>::
    object_type_name = types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"VectorParseTree")}};
}

namespace afc::editor {
/* static */ const ParserId& ParserId::Text() {
  static const ParserId* output =
      new ParserId{NON_EMPTY_SINGLE_LINE_CONSTANT(L"text")};
  return *output;
}

/* static */ const ParserId& ParserId::Cpp() {
  static const ParserId* output =
      new ParserId{NON_EMPTY_SINGLE_LINE_CONSTANT(L"cpp")};
  return *output;
}

/* static */ const ParserId& ParserId::Diff() {
  static const ParserId* output =
      new ParserId{NON_EMPTY_SINGLE_LINE_CONSTANT(L"diff")};
  return *output;
}

/* static */ const ParserId& ParserId::Markdown() {
  static const ParserId* output =
      new ParserId{NON_EMPTY_SINGLE_LINE_CONSTANT(L"md")};
  return *output;
}

/* static */ const ParserId& ParserId::Csv() {
  static const ParserId* output =
      new ParserId{NON_EMPTY_SINGLE_LINE_CONSTANT(L"csv")};
  return *output;
}

/* static */ const ParserId& ParserId::Py() {
  static const ParserId* output =
      new ParserId{NON_EMPTY_SINGLE_LINE_CONSTANT(L"py")};
  return *output;
}

/*static*/ const ParseTreeProperty& ParseTreeProperty::Link() {
  static const auto* output =
      new ParseTreeProperty{NON_EMPTY_SINGLE_LINE_CONSTANT(L"link")};
  return *output;
}

/*static*/ const ParseTreeProperty& ParseTreeProperty::LinkTarget() {
  static const auto* output =
      new ParseTreeProperty{NON_EMPTY_SINGLE_LINE_CONSTANT(L"link_target")};
  return *output;
}

/*static*/ const ParseTreeProperty& ParseTreeProperty::TableCell(size_t id) {
  static const std::vector<ParseTreeProperty>* values = [] {
    auto output = new std::vector<ParseTreeProperty>();
    for (int i = 0; i < 32; i++)
      output->push_back(
          ParseTreeProperty(NON_EMPTY_SINGLE_LINE_CONSTANT(L"table_cell_") +
                            NonEmptySingleLine(i)));
    return output;
  }();
  if (id < values->size()) return values->at(id);
  // TODO(easy, 2023-09-16): Would be good to be able to support this better.
  static const ParseTreeProperty output{
      NON_EMPTY_SINGLE_LINE_CONSTANT(L"table_cell_infty")};
  return output;
}

/*static*/ const ParseTreeProperty& ParseTreeProperty::CellContent() {
  static const auto* output =
      new ParseTreeProperty{NON_EMPTY_SINGLE_LINE_CONSTANT(L"cell_content")};
  return *output;
}

/*static*/ const ParseTreeProperty& ParseTreeProperty::StringValue() {
  static const auto* output =
      new ParseTreeProperty{NON_EMPTY_SINGLE_LINE_CONSTANT(L"string_value")};
  return *output;
}

/*static*/ const ParseTreeProperty& ParseTreeProperty::NumberValue() {
  static const auto* output =
      new ParseTreeProperty{NON_EMPTY_SINGLE_LINE_CONSTANT(L"number_value")};
  return *output;
}

std::ostream& operator<<(std::ostream& os, const ParseTree& t) {
  os << "[ParseTree: " << t.range() << ", children: ";
  for (auto& c : t.children()) {
    os << c;
  }
  os << "]";
  return os;
}

ParseTree::ParseTree(Range range) : range_(std::move(range)) {}

ParseTree::ParseTree(const ParseTree& other)
    : children_(other.children()),
      children_hashes_(other.children_hashes_),
      range_(other.range()),
      depth_(other.depth()),
      modifiers_(other.modifiers()),
      properties_(other.properties()) {}

Range ParseTree::range() const { return range_; }
void ParseTree::set_range(Range range) { range_ = range; }

size_t ParseTree::depth() const { return depth_; }

const LineModifierSet& ParseTree::modifiers() const { return modifiers_; }

void ParseTree::set_modifiers(LineModifierSet modifiers) {
  modifiers_ = std::move(modifiers);
}

void ParseTree::InsertModifier(LineModifier modifier) {
  modifiers_.insert(modifier);
}

const std::vector<ParseTree>& ParseTree::children() const { return children_; }

// TODO(easy): Check if there's a bug here. Imagine that the deepest children is
// modified and now it's given a smaller depth; won't we retain the old depth
// (rather than adjust it to the smaller integer)?
std::unique_ptr<ParseTree, std::function<void(ParseTree*)>>
ParseTree::MutableChildren(size_t i) {
  CHECK_LT(i, children_.size());
  XorChildHash(i);  // Remove its old hash.
  return std::unique_ptr<ParseTree, std::function<void(ParseTree*)>>(
      &children_[i], [this, i](ParseTree* child) {
        depth_ = std::max(depth(), child->depth() + 1);
        XorChildHash(i);  // Add its new hash.
      });
}

void ParseTree::XorChildHash(size_t position) {
  children_hashes_ ^= hash_combine(position, children_[position].hash());
}

void ParseTree::Reset() {
  children_.clear();
  children_hashes_ = 0;
  depth_ = 0;
  set_modifiers(LineModifierSet());
}

void ParseTree::PushChild(ParseTree child) {
  depth_ = std::max(depth(), child.depth() + 1);
  children_.push_back(std::move(child));
  XorChildHash(children_.size() - 1);
}

size_t ParseTree::hash() const {
  return language::hash_combine(
      compute_hash(range_, MakeHashableIteratorRange(modifiers_),
                   MakeHashableIteratorRange(properties_)),
      children_hashes_);
}

void ParseTree::set_properties(
    std::unordered_set<ParseTreeProperty> properties) {
  properties_ = std::move(properties);
}

const std::unordered_set<ParseTreeProperty>& ParseTree::properties() const {
  return properties_;
}

ParseTree SimplifyTree(const ParseTree& tree) {
  ParseTree output(tree.range());
  for (const auto& child : tree.children()) {
    if (child.range().begin().line != child.range().end().line) {
      output.PushChild(SimplifyTree(child));
    }
  }
  return output;
}

namespace {
std::optional<ParseTree> ZoomOutTree(const ParseTree& input, double ratio) {
  // TODO(trivial, 2023-10-10): The two lines below shouldn't need the call to
  // `read`: instead, ghost_type should declare the `operator*` overload.
  Range range =
      Range(LineColumn(LineNumber(input.range().begin().line.read() * ratio)),
            LineColumn(LineNumber(input.range().end().line.read() * ratio)));
  if (range.begin().line == range.end().line) {
    return std::nullopt;
  }
  ParseTree output(range);
  for (const auto& child : input.children()) {
    auto output_child = ZoomOutTree(child, ratio);
    if (output_child.has_value()) {
      output.PushChild(std::move(output_child.value()));
    }
  }
  return output;
}
}  // namespace

ParseTree ZoomOutTree(const ParseTree& input, LineNumberDelta input_lines,
                      LineNumberDelta output_lines) {
  LOG(INFO) << "Zooming out: " << input_lines << " to " << output_lines;
  auto output = ZoomOutTree(
      input, static_cast<double>(output_lines.read()) / input_lines.read());
  if (!output.has_value()) {
    return ParseTree();
  }

  return output.value();
}

// Returns the first children of tree that ends after a given position.
size_t FindChildrenForPosition(const ParseTree* tree,
                               const LineColumn& position) {
  for (size_t i = 0; i < tree->children().size(); i++) {
    if (tree->children().at(i).range().Contains(position)) {
      return i;
    }
  }
  return tree->children().size();
}

ParseTree::Route FindRouteToPosition(const ParseTree& root,
                                     const LineColumn& position) {
  ParseTree::Route output;
  auto tree = &root;
  for (;;) {
    size_t index = FindChildrenForPosition(tree, position);
    if (index == tree->children().size()) {
      return output;
    }
    output.push_back(index);
    tree = &tree->children().at(index);
  }
}

std::vector<const ParseTree*> MapRoute(const ParseTree& root,
                                       const ParseTree::Route& route) {
  std::vector<const ParseTree*> output = {&root};
  for (auto& index : route) {
    output.push_back(&output.back()->children().at(index));
  }
  return output;
}

const ParseTree& FollowRoute(const ParseTree& root,
                             const ParseTree::Route& route) {
  const ParseTree* tree = &root;
  for (size_t index : route) {
    CHECK_LT(index, tree->children().size());
    tree = &tree->children().at(index);
  }
  return *tree;
}

namespace {
bool Contains(const std::unordered_set<NonEmptySingleLine>& values,
              const SingleLine& pattern) {
  return std::visit(overload{[](Error) { return false; },
                             [&values](NonEmptySingleLine non_empty_pattern) {
                               return values.contains(non_empty_pattern);
                             }},
                    NonEmptySingleLine::New(pattern));
}

class NullTreeParser : public TreeParser {
 public:
  ParseTree FindChildren(const LineSequence&, Range range) override {
    return ParseTree(range);
  }
};

class WordsTreeParser : public TreeParser {
  const std::unordered_set<wchar_t> symbol_characters_;
  const std::unordered_set<NonEmptySingleLine> typos_;
  const NonNull<std::unique_ptr<TreeParser>> delegate_;

 public:
  WordsTreeParser(LazyString symbol_characters,
                  std::unordered_set<NonEmptySingleLine> typos,
                  NonNull<std::unique_ptr<TreeParser>> delegate)
      : symbol_characters_(
            container::MaterializeUnorderedSet(symbol_characters)),
        typos_(typos),
        delegate_(std::move(delegate)) {}

  ParseTree FindChildren(const LineSequence& buffer, Range range) override {
    ParseTree output(range);
    range.ForEachLine([&](LineNumber line) {
      const Line& contents = buffer.at(line);

      ColumnNumber line_end = contents.EndColumn();
      if (line == range.end().line) {
        line_end = std::min(line_end, range.end().column);
      }

      ColumnNumber column =
          line == range.begin().line ? range.begin().column : ColumnNumber(0);
      while (column < line_end) {
        while (column < line_end && IsSpace(contents, column)) ++column;
        ColumnNumber begin = column;

        while (column < line_end && !IsSpace(contents, column)) ++column;
        if (begin == column) return;

        SingleLine keyword =
            contents.contents().Substring(begin, column - begin);
        ParseTree child = delegate_->FindChildren(
            buffer, LineRange(LineColumn(line, begin), column - begin).read());
        if (Contains(typos_, keyword)) child.InsertModifier(LineModifier::kRed);
        DVLOG(6) << "Adding word: " << child;
        output.PushChild(std::move(child));
      }
    });
    return output;
  }

 private:
  bool IsSpace(const Line& line, ColumnNumber column) {
    return !symbol_characters_.contains(line.get(column));
  }
};

class LineTreeParser : public TreeParser {
 public:
  LineTreeParser(NonNull<std::unique_ptr<TreeParser>> delegate)
      : delegate_(std::move(delegate)) {}

  ParseTree FindChildren(const LineSequence& buffer, Range range) override {
    ParseTree output(range);
    range.ForEachLine([&](LineNumber line) {
      auto contents = buffer.at(line);
      if (contents.empty()) {
        return;
      }

      output.PushChild(delegate_->FindChildren(
          buffer, Range(LineColumn(line),
                        std::min(LineColumn(line, contents.EndColumn()),
                                 range.end()))));
    });
    return output;
  }

 private:
  const NonNull<std::unique_ptr<TreeParser>> delegate_;
};

}  // namespace

/* static */ bool TreeParser::IsNull(TreeParser* parser) {
  return dynamic_cast<NullTreeParser*>(parser) != nullptr;
}

NonNull<std::unique_ptr<TreeParser>> NewNullTreeParser() {
  return NonNull<std::unique_ptr<NullTreeParser>>();
}

NonNull<std::unique_ptr<TreeParser>> NewWordsTreeParser(
    LazyString symbol_characters, std::unordered_set<NonEmptySingleLine> typos,
    NonNull<std::unique_ptr<TreeParser>> delegate) {
  return MakeNonNullUnique<WordsTreeParser>(
      std::move(symbol_characters), std::move(typos), std::move(delegate));
}

NonNull<std::unique_ptr<TreeParser>> NewLineTreeParser(
    NonNull<std::unique_ptr<TreeParser>> delegate) {
  return MakeNonNullUnique<LineTreeParser>(std::move(delegate));
}

void RegisterParseTreeFunctions(language::gc::Pool& pool,
                                vm::Environment& environment) {
  namespace gc = language::gc;

  using vm::ObjectType;
  using vm::PurityType;

  gc::Root<ObjectType> parse_tree_object_type = ObjectType::New(
      pool, vm::VMTypeMapper<
                NonNull<std::shared_ptr<const ParseTree>>>::object_type_name);

  parse_tree_object_type.ptr()->AddField(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"children")},
      vm::NewCallback(
          pool, kPurityTypeReader,
          [](NonNull<std::shared_ptr<const ParseTree>> tree) {
            std::vector<NonNull<std::shared_ptr<const ParseTree>>> output;
            for (const ParseTree& child : tree->children())
              // TODO(2023-09-16): Find a way to avoid Unsafe here: that means
              // figuring out how to use the std::shared_ptr aliasing
              // constructor with NonNull.
              output.push_back(
                  NonNull<std::shared_ptr<const ParseTree>>::Unsafe(
                      std::shared_ptr<const ParseTree>(tree.get_shared(),
                                                       &child)));
            return MakeNonNullShared<Protected<
                std::vector<NonNull<std::shared_ptr<const ParseTree>>>>>(
                Protected(std::move(output)));
          })
          .ptr());

  parse_tree_object_type.ptr()->AddField(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"range")},
      vm::NewCallback(pool, kPurityTypeReader,
                      [](NonNull<std::shared_ptr<const ParseTree>> tree) {
                        return tree->range();
                      })
          .ptr());

  parse_tree_object_type.ptr()->AddField(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"properties")},
      vm::NewCallback(
          pool, kPurityTypeReader,
          [](NonNull<std::shared_ptr<const ParseTree>> tree) {
            return MakeNonNullShared<Protected<std::set<LazyString>>>(
                MakeProtected(container::MaterializeSet(
                    tree->properties() |
                    std::views::transform(
                        [](const ParseTreeProperty& property) {
                          return ToLazyString(property);
                        }))));
          })
          .ptr());

  environment.DefineType(parse_tree_object_type.ptr());
  vm::container::Export<
      typename std::vector<NonNull<std::shared_ptr<const ParseTree>>>>(
      pool, environment);
}

ValueOrError<URL> FindLinkTarget(const ParseTree& tree,
                                 const LineSequence& contents) {
  if (tree.properties().find(ParseTreeProperty::LinkTarget()) !=
      tree.properties().end())
    return URL::New(NonEmptySingleLine::New(
        SingleLine::New(contents.ViewRange(tree.range()).ToLazyString())));
  for (const auto& child : tree.children())
    if (ValueOrError<URL> output = FindLinkTarget(child, contents);
        std::holds_alternative<URL>(output))
      return output;
  return Error{LazyString{L"Unable to find link."}};
}
}  // namespace afc::editor
namespace afc::vm {
namespace gc = language::gc;
using editor::ParseTree;
using language::MakeNonNullShared;
using language::NonNull;
namespace {
struct ParseTreeWrapper {
  const NonNull<std::shared_ptr<const ParseTree>> tree;
};
}  // namespace

/* static */ NonNull<std::shared_ptr<const editor::ParseTree>> vm::VMTypeMapper<
    NonNull<std::shared_ptr<const editor::ParseTree>>>::get(Value& value) {
  return value.get_user_value<ParseTreeWrapper>(object_type_name).value().tree;
}

/* static */ gc::Root<vm::Value>
VMTypeMapper<NonNull<std::shared_ptr<const editor::ParseTree>>>::New(
    gc::Pool& pool, NonNull<std::shared_ptr<const editor::ParseTree>> value) {
  return vm::Value::NewObject(
      pool, object_type_name,
      MakeNonNullShared<ParseTreeWrapper>(ParseTreeWrapper{.tree = value}));
}

const vm::types::ObjectName vm::VMTypeMapper<
    NonNull<std::shared_ptr<const ParseTree>>>::object_type_name =
    vm::types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ParseTree")}};

}  // namespace afc::vm
