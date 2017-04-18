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
    for (auto line = root->range.begin.line; line <= root->range.end.line; line++) {
      const auto& contents = *buffer.at(line);

      size_t line_end = contents.size();
      if (line == root->range.end.line) {
        line_end = min(line_end, root->range.end.column);
      }

      size_t column = line == root->range.begin.line ? root->range.begin.column : 0;
      while (column < line_end) {
        ParseTree new_children;

        while (column < line_end && IsSpace(contents, column)) { column++; }
        new_children.range.begin = LineColumn(line, column);

        while (column < line_end && !IsSpace(contents, column)) { column++; }
        new_children.range.end = LineColumn(line, column);

        if (new_children.range.IsEmpty()) { return; }

        DVLOG(6) << "Adding word: " << new_children;
        root->children.push_back(new_children);
        delegate_->FindChildren(buffer, &root->children.back());
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
    for (auto line = root->range.begin.line; line <= root->range.end.line; line++) {
      auto contents = buffer.at(line);

      ParseTree new_children;
      new_children.range.begin = LineColumn(line);
      new_children.range.end = min(LineColumn(line + 1), root->range.end);
      DVLOG(5) << "Adding line: " << new_children;
      root->children.push_back(new_children);
      delegate_->FindChildren(buffer, &root->children.back());
    }
  }

 private:
  const std::unique_ptr<TreeParser> delegate_;
};

}  // namespace

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