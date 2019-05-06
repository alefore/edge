#include "src/parse_tree.h"

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const ParseTree& t) {
  os << "[ParseTree: " << t.range << ", children: ";
  for (auto& c : t.children) {
    os << c;
  }
  os << "]";
  return os;
}

size_t ParseTree::depth() const { return depth_; }

void ParseTree::Reset() {
  children.clear();
  depth_ = 0;
}

std::unique_ptr<ParseTree, std::function<void(ParseTree*)>>
ParseTree::PushChild() {
  children.emplace_back();
  return std::unique_ptr<ParseTree, std::function<void(ParseTree*)>>(
      &children.back(),
      [this](ParseTree* child) { depth_ = max(depth(), child->depth() + 1); });
}

void SimplifyTree(const ParseTree& tree, ParseTree* output) {
  output->range = tree.range;
  for (const auto& child : tree.children) {
    if (child.range.begin.line != child.range.end.line) {
      SimplifyTree(child, output->PushChild().get());
    }
  }
}

namespace {
void ZoomOutTree(const ParseTree& input, double ratio, ParseTree* parent) {
  Range range = input.range;
  range.begin.line.line *= ratio;
  range.end.line.line *= ratio;
  if (range.begin.line == range.end.line) {
    return;
  }
  auto output = parent->PushChild();
  output->range = range;
  for (const auto& child : input.children) {
    ZoomOutTree(child, ratio, output.get());
  }
}
}  // namespace

ParseTree ZoomOutTree(const ParseTree& input, LineNumberDelta input_lines,
                      LineNumberDelta output_lines) {
  LOG(INFO) << "Zooming out: " << input_lines << " to " << output_lines;
  ParseTree output;
  ZoomOutTree(
      input,
      static_cast<double>(output_lines.line_delta) / input_lines.line_delta,
      &output);
  if (output.children.empty()) {
    return ParseTree();
  }

  CHECK_EQ(output.children.size(), 1ul);
  return std::move(output.children.at(0));
}

// Returns the first children of tree that ends after a given position.
size_t FindChildrenForPosition(const ParseTree* tree,
                               const LineColumn& position) {
  for (size_t i = 0; i < tree->children.size(); i++) {
    if (tree->children.at(i).range.Contains(position)) {
      return i;
    }
  }
  return tree->children.size();
}

ParseTree::Route FindRouteToPosition(const ParseTree& root,
                                     const LineColumn& position) {
  ParseTree::Route output;
  auto tree = &root;
  for (;;) {
    size_t index = FindChildrenForPosition(tree, position);
    if (index == tree->children.size()) {
      return output;
    }
    output.push_back(index);
    tree = &tree->children.at(index);
  }
}

std::vector<const ParseTree*> MapRoute(const ParseTree& root,
                                       const ParseTree::Route& route) {
  std::vector<const ParseTree*> output = {&root};
  for (auto& index : route) {
    output.push_back(&output.back()->children.at(index));
  }
  return output;
}

const ParseTree* FollowRoute(const ParseTree& root,
                             const ParseTree::Route& route) {
  auto tree = &root;
  for (auto& index : route) {
    CHECK_LT(index, tree->children.size());
    tree = &tree->children.at(index);
  }
  return tree;
}

namespace {
class NullTreeParser : public TreeParser {
 public:
  void FindChildren(const BufferContents&, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
  }
};

class CharTreeParser : public TreeParser {
 public:
  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    for (auto line = root->range.begin.line; line <= root->range.end.line;
         line++) {
      CHECK_LE(line, buffer.EndLine());
      auto contents = buffer.at(line);
      ColumnNumber end_column =
          line == root->range.end.line
              ? root->range.end.column
              : contents->EndColumn() + ColumnNumberDelta(1);
      for (ColumnNumber i = line == root->range.begin.line
                                ? root->range.begin.column
                                : ColumnNumber(0);
           i < end_column; ++i) {
        ParseTree new_children;
        new_children.range = Range::InLine(line, i, ColumnNumberDelta(1));
        DVLOG(7) << "Adding char: " << new_children;
        root->children.push_back(new_children);
      }
    }
  }
};

class WordsTreeParser : public TreeParser {
 public:
  WordsTreeParser(std::wstring symbol_characters,
                  std::unordered_set<wstring> typos,
                  std::unique_ptr<TreeParser> delegate)
      : symbol_characters_(symbol_characters),
        typos_(typos),
        delegate_(std::move(delegate)) {}

  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    for (auto line = root->range.begin.line; line <= root->range.end.line;
         line++) {
      const auto& contents = *buffer.at(line);

      ColumnNumber line_end = contents.EndColumn();
      if (line == root->range.end.line) {
        line_end = min(line_end, root->range.end.column);
      }

      ColumnNumber column = line == root->range.begin.line
                                ? root->range.begin.column
                                : ColumnNumber(0);
      while (column < line_end) {
        auto new_children = root->PushChild();

        while (column < line_end && IsSpace(contents, column)) {
          column++;
        }
        new_children->range.begin = LineColumn(line, column);

        while (column < line_end && !IsSpace(contents, column)) {
          column++;
        }
        new_children->range.end = LineColumn(line, column);

        if (new_children->range.IsEmpty()) {
          return;
        }

        CHECK_GT(new_children->range.end.column,
                 new_children->range.begin.column);
        auto keyword =
            Substring(contents.contents(), new_children->range.begin.column,
                      new_children->range.end.column -
                          new_children->range.begin.column)
                ->ToString();
        if (typos_.find(keyword) != typos_.end()) {
          new_children->modifiers.insert(LineModifier::RED);
        }
        DVLOG(6) << "Adding word: " << *new_children;
        delegate_->FindChildren(buffer, new_children.get());
      }
    }
  }

 private:
  bool IsSpace(const Line& line, ColumnNumber column) {
    return symbol_characters_.find(line.get(column)) == symbol_characters_.npos;
  }

  const std::wstring symbol_characters_;
  const std::unordered_set<wstring> typos_;
  const std::unique_ptr<TreeParser> delegate_;
};

class LineTreeParser : public TreeParser {
 public:
  LineTreeParser(std::unique_ptr<TreeParser> delegate)
      : delegate_(std::move(delegate)) {}

  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    DVLOG(5) << "Finding lines: " << *root;
    for (auto line = root->range.begin.line; line <= root->range.end.line;
         line++) {
      auto contents = buffer.at(line);
      if (contents->empty()) {
        continue;
      }

      auto new_children = root->PushChild();
      new_children->range.begin = LineColumn(line);
      new_children->range.end =
          min(LineColumn(line, contents->EndColumn()), root->range.end);
      DVLOG(5) << "Adding line: " << *new_children;
      delegate_->FindChildren(buffer, new_children.get());
    }
  }

 private:
  const std::unique_ptr<TreeParser> delegate_;
};

}  // namespace

/* static */ bool TreeParser::IsNull(TreeParser* parser) {
  return dynamic_cast<NullTreeParser*>(parser) != nullptr;
}

std::unique_ptr<TreeParser> NewNullTreeParser() {
  return std::make_unique<NullTreeParser>();
}

std::unique_ptr<TreeParser> NewCharTreeParser() {
  return std::make_unique<CharTreeParser>();
}

std::unique_ptr<TreeParser> NewWordsTreeParser(
    wstring symbol_characters, std::unordered_set<wstring> typos,
    std::unique_ptr<TreeParser> delegate) {
  return std::make_unique<WordsTreeParser>(
      std::move(symbol_characters), std::move(typos), std::move(delegate));
}

std::unique_ptr<TreeParser> NewLineTreeParser(
    std::unique_ptr<TreeParser> delegate) {
  return std::make_unique<LineTreeParser>(std::move(delegate));
}

}  // namespace editor
}  // namespace afc
