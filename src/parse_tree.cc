#include "parse_tree.h"

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "buffer.h"

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

std::unique_ptr<ParseTree, std::function<void(ParseTree*)>>
PushChild(ParseTree* parent) {  parent->children.emplace_back();
  return std::unique_ptr<ParseTree, std::function<void(ParseTree*)>>(
      &parent->children.back(),
      [parent](ParseTree* child) {
        parent->depth = max(parent->depth, child->depth + 1);
      });
}

void SimplifyTree(const ParseTree& tree, ParseTree* output) {
  output->range = tree.range;
  for (const auto& child : tree.children) {
    if (child.range.begin.line != child.range.end.line) {
      SimplifyTree(child, PushChild(output).get());
    }
  }
}

namespace {
void ZoomOutTree(const ParseTree& input, double ratio, ParseTree* output) {
  output->range = input.range;
  output->range.begin.line *= ratio;
  output->range.end.line *= ratio;
  for (const auto& child : input.children) {
    ZoomOutTree(child, ratio, PushChild(output).get());
  }
}
}  // namespace

ParseTree ZoomOutTree(
    const ParseTree& input, size_t input_lines, size_t output_lines) {
  LOG(INFO) << "Zooming out: " << input_lines << " to " << output_lines;
  ParseTree first_pass;
  ZoomOutTree(input, static_cast<double>(output_lines) / input_lines,
              &first_pass);
  ParseTree output;
  SimplifyTree(first_pass, &output);
  return output;
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

ParseTree::Route FindRouteToPosition(
    const ParseTree& root, const LineColumn& position) {
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

std::vector<const ParseTree*> MapRoute(
    const ParseTree& root, const ParseTree::Route& route) {
  std::vector<const ParseTree*> output = { &root };
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
      CHECK_LT(line, buffer.size());
      auto contents = buffer.at(line);
      size_t end_column = line == root->range.end.line
                              ? root->range.end.column
                              : contents->size() + 1;
      for (size_t i = line == root->range.begin.line
                          ? root->range.begin.column
                          : 0;
           i < end_column; i++) {
        ParseTree new_children;
        new_children.range = Range::InLine(line, i, 1);
        DVLOG(7) << "Adding char: " << new_children;
        root->children.push_back(new_children);
      }
    }
  }
};

class WordsTreeParser : public TreeParser {
 public:
  WordsTreeParser(std::wstring word_characters,
                  std::unique_ptr<TreeParser> delegate)
      : word_characters_(word_characters), delegate_(std::move(delegate)) {}

  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    for (auto line = root->range.begin.line;
         line <= root->range.end.line; line++) {
      const auto& contents = *buffer.at(line);

      size_t line_end = contents.size();
      if (line == root->range.end.line) {
        line_end = min(line_end, root->range.end.column);
      }

      size_t column =
          line == root->range.begin.line ? root->range.begin.column : 0;
      while (column < line_end) {
        auto new_children = PushChild(root);

        while (column < line_end && IsSpace(contents, column)) { column++; }
        new_children->range.begin = LineColumn(line, column);

        while (column < line_end && !IsSpace(contents, column)) { column++; }
        new_children->range.end = LineColumn(line, column);

        if (new_children->range.IsEmpty()) { return; }

        DVLOG(6) << "Adding word: " << *new_children;
        delegate_->FindChildren(buffer, new_children.get());
      }
    }
  }

 private:
  bool IsSpace(const Line& line, size_t column) {
    return word_characters_.find(line.get(column)) == word_characters_.npos;
  }

  const std::wstring word_characters_;
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
    for (auto line = root->range.begin.line;
         line <= root->range.end.line; line++) {
      auto contents = buffer.at(line);
      if (contents->empty()) {
        continue;
      }

      auto new_children = PushChild(root);
      new_children->range.begin = LineColumn(line);
      new_children->range.end =
          min(LineColumn(line, contents->size()), root->range.end);
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
  return std::unique_ptr<TreeParser>(new NullTreeParser());
}

std::unique_ptr<TreeParser> NewCharTreeParser() {
  return std::unique_ptr<TreeParser>(new CharTreeParser());
}

std::unique_ptr<TreeParser> NewWordsTreeParser(
    wstring word_characters, std::unique_ptr<TreeParser> delegate) {
  return std::unique_ptr<TreeParser>(
      new WordsTreeParser(word_characters, std::move(delegate)));
}

std::unique_ptr<TreeParser> NewLineTreeParser(
    std::unique_ptr<TreeParser> delegate) {
  return std::unique_ptr<TreeParser>(new LineTreeParser(std::move(delegate)));
}

}  // namespace editor
}  // namespace afc
