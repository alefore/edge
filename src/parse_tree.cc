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
    os << "[ParseTree: [" << t.begin << ", " << t.end << "), children: ";
    for (auto& c : t.children) {
      os << c;
    }
    os << "]";
    return os;
}

namespace {
class NullTreeParser : public TreeParser {
 public:
  void FindChildren(const OpenBuffer&, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
  }
};

class CharTreeParser : public TreeParser {
 public:
  void FindChildren(const OpenBuffer& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    for (auto line = root->begin.line; line <= root->end.line; line++) {
      CHECK_LT(line, buffer.contents()->size());
      auto contents = buffer.contents()->at(line);
      size_t end_column =
          line == root->end.line ? root->end.column : contents->size() + 1;
      for (size_t i = line == root->begin.line ? root->begin.column : 0;
           i < end_column; i++) {
        ParseTree new_children;
        new_children.begin = LineColumn(line, i);
        new_children.end = LineColumn(line, i + 1);
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

  void FindChildren(const OpenBuffer& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    for (auto line = root->begin.line; line <= root->end.line; line++) {
      auto contents = buffer.contents()->at(line);

      size_t line_end = contents->size();
      if (line == root->end.line) {
        line_end = min(line_end, root->end.column);
      }

      size_t column = line == root->begin.line ? root->begin.column : 0;
      while (column < line_end) {
        size_t word_start = column;

        // Skip any non-space characters.
        while (column < line_end
               && word_characters_.find(contents->get(column))
                  != word_characters_.npos) {
          column++;
        }

        // Skip any space characters.
        while (column < line_end
               && word_characters_.find(contents->get(column))
                  == word_characters_.npos) {
          column++;
        }

        CHECK_GT(column, word_start);  // Assert that we made progress.

        ParseTree new_children;
        new_children.begin = LineColumn(line, word_start);
        new_children.end = LineColumn(line, column);
        if (column == line_end && line < root->end.line) {
          new_children.end = LineColumn(line + 1);
        }
        DVLOG(6) << "Adding word: " << new_children;
        root->children.push_back(new_children);
        delegate_->FindChildren(buffer, &root->children.back());
      }
    }
  }

 private:
  const std::wstring word_characters_;
  const std::unique_ptr<TreeParser> delegate_;
};

class LineTreeParser : public TreeParser {
 public:
  LineTreeParser(std::unique_ptr<TreeParser> delegate)
      : delegate_(std::move(delegate)) {}

  void FindChildren(const OpenBuffer& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    DVLOG(5) << "Finding lines: " << *root;
    for (auto line = root->begin.line; line <= root->end.line; line++) {
      auto contents = buffer.contents()->at(line);

      ParseTree new_children;
      new_children.begin = LineColumn(line);
      new_children.end = min(LineColumn(line + 1), root->end);
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