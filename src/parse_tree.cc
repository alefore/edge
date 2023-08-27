#include "src/parse_tree.h"

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/language/hash.h"
#include "src/language/lazy_string/substring.h"

namespace afc::editor {
using language::compute_hash;
using language::hash_combine;
using language::MakeHashableIteratorRange;
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::Range;

/*static*/ const ParseTreeProperty& ParseTreeProperty::Link() {
  static const auto* output = new ParseTreeProperty(L"link");
  return *output;
}

/*static*/ const ParseTreeProperty& ParseTreeProperty::LinkTarget() {
  static const auto* output = new ParseTreeProperty(L"link_target");
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
    if (child.range().begin.line != child.range().end.line) {
      output.PushChild(SimplifyTree(child));
    }
  }
  return output;
}

namespace {
std::optional<ParseTree> ZoomOutTree(const ParseTree& input, double ratio) {
  Range range = input.range();
  range.begin.line *= ratio;
  range.end.line *= ratio;
  if (range.begin.line == range.end.line) {
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
  auto tree = &root;
  for (auto& index : route) {
    CHECK_LT(index, tree->children().size());
    tree = &tree->children().at(index);
  }
  return *tree;
}

namespace {
class NullTreeParser : public TreeParser {
 public:
  ParseTree FindChildren(const BufferContents&, Range range) override {
    return ParseTree(range);
  }
};

class WordsTreeParser : public TreeParser {
 public:
  WordsTreeParser(std::wstring symbol_characters,
                  std::unordered_set<std::wstring> typos,
                  NonNull<std::unique_ptr<TreeParser>> delegate)
      : symbol_characters_(symbol_characters),
        typos_(typos),
        delegate_(std::move(delegate)) {}

  ParseTree FindChildren(const BufferContents& buffer, Range range) override {
    ParseTree output(range);
    range.ForEachLine([&](LineNumber line) {
      const Line& contents = buffer.at(line).value();

      ColumnNumber line_end = contents.EndColumn();
      if (line == range.end.line) {
        line_end = std::min(line_end, range.end.column);
      }

      ColumnNumber column =
          line == range.begin.line ? range.begin.column : ColumnNumber(0);
      while (column < line_end) {
        Range keyword_range;
        while (column < line_end && IsSpace(contents, column)) {
          column++;
        }
        keyword_range.begin = LineColumn(line, column);

        while (column < line_end && !IsSpace(contents, column)) {
          column++;
        }
        keyword_range.end = LineColumn(line, column);

        if (keyword_range.IsEmpty()) {
          return;
        }

        CHECK_GT(keyword_range.end.column, keyword_range.begin.column);
        // TODO(2022-04-22): Find a way to avoid the call to ToString?
        auto keyword =
            Substring(contents.contents(), keyword_range.begin.column,
                      keyword_range.end.column - keyword_range.begin.column)
                ->ToString();
        ParseTree child = delegate_->FindChildren(buffer, keyword_range);
        if (typos_.find(keyword) != typos_.end()) {
          child.InsertModifier(LineModifier::kRed);
        }
        DVLOG(6) << "Adding word: " << child;
        output.PushChild(std::move(child));
      }
    });
    return output;
  }

 private:
  bool IsSpace(const Line& line, ColumnNumber column) {
    return symbol_characters_.find(line.get(column)) == symbol_characters_.npos;
  }

  const std::wstring symbol_characters_;
  const std::unordered_set<std::wstring> typos_;
  const NonNull<std::unique_ptr<TreeParser>> delegate_;
};

class LineTreeParser : public TreeParser {
 public:
  LineTreeParser(NonNull<std::unique_ptr<TreeParser>> delegate)
      : delegate_(std::move(delegate)) {}

  ParseTree FindChildren(const BufferContents& buffer, Range range) override {
    ParseTree output(range);
    range.ForEachLine([&](LineNumber line) {
      auto contents = buffer.at(line);
      if (contents->empty()) {
        return;
      }

      output.PushChild(delegate_->FindChildren(
          buffer,
          Range(LineColumn(line),
                std::min(LineColumn(line, contents->EndColumn()), range.end))));
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
    std::wstring symbol_characters, std::unordered_set<std::wstring> typos,
    NonNull<std::unique_ptr<TreeParser>> delegate) {
  return MakeNonNullUnique<WordsTreeParser>(
      std::move(symbol_characters), std::move(typos), std::move(delegate));
}

NonNull<std::unique_ptr<TreeParser>> NewLineTreeParser(
    NonNull<std::unique_ptr<TreeParser>> delegate) {
  return MakeNonNullUnique<LineTreeParser>(std::move(delegate));
}

}  // namespace afc::editor
