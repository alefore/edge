#include "cpp_parse_tree.h"

#include <glog/logging.h>

#include "buffer.h"

namespace afc {
namespace editor {

namespace {
class CppTreeParser : public TreeParser {
 public:
  void FindChildren(const OpenBuffer& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    LineColumn position = root->begin;
    while (position < root->end) {
      auto old_position = position;
      switch (buffer.character_at(position)) {
        case '#':
          {
            ParseTree child;
            child.begin = position;
            child.end = AdvanceUntilEndOfLine(buffer, position);
            root->children.push_back(child);

            position = child.end;
          }
          continue;

        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
        case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
        case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
        case 'V': case 'W': case 'X': case 'Y': case 'Z':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
        case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
        case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
        case 'v': case 'w': case 'x': case 'y': case 'z':
        case '(':
        case '{':
        case '"':
          {
            root->children.push_back(ParseTree());
            root->children.back().begin = position;
            ConsumeBlock(buffer, &root->children.back(), root->end);
            position = root->children.back().end;
          }
          continue;

        case ' ':
        case '\n':
        default:
          // TODO: Log if unexpected?
          position = Advance(buffer, position);
          continue;
      }
      CHECK_LT(old_position, position);
    }
  }

 private:
  void ConsumeBlock(const OpenBuffer& buffer, ParseTree* block,
                    LineColumn limit) {
    LOG(INFO) << "Parsing block at position: " << block->begin;

    auto c = buffer.character_at(block->begin);
    static const wstring id = L"_abcdefghijklmnopqrstuvwxyz";

    if (id.find(tolower(c)) != id.npos) {
      block->end = AdvanceUntil(
          buffer, block->begin, limit,
          [&id](wchar_t c) {
            return id.find(tolower(c)) == id.npos;
          });
      return;
    }

    if (c == L'(' || c == L'{') {
      auto position = Advance(buffer, block->begin);
      while (true) {
        // Skip spaces.
        position = AdvanceUntil(
            buffer, position, limit, [](wchar_t c) { return !iswspace(c); });
        if (position == limit) {
          block->end = limit;
          return;
        }

        if ((c == L'(' && buffer.character_at(position) == L')')
            || (c == L'{' && buffer.character_at(position) == L'}')) {
          block->end = Advance(buffer, position);
          return;
        }

        block->children.push_back(ParseTree());
        block->children.back().begin = position;
        ConsumeBlock(buffer, &block->children.back(), limit);
        if (position == block->children.back().end) {
          block->end = position;
          return;  // Didn't advance.
        }

        position = block->children.back().end;
      }
      return;
    }

    if (c == '/' && buffer.character_at(Advance(buffer, block->begin)) == '/') {
      block->end = AdvanceUntilEndOfLine(buffer, block->begin);
      words_parser_->FindChildren(buffer, block);
      return;
    }

    if (c == L'"' || c == L'\'') {
      block->end = Advance(buffer, block->begin);
      while (buffer.character_at(block->end) != c && block->end < limit) {
        if (buffer.character_at(block->end) == '\\') {
          block->end = Advance(buffer, block->end);
        }
        block->end = Advance(buffer, block->end);
      }
      if (block->end < limit) {
        block->end = Advance(buffer, block->end);
      }
      if (c == L'\"') {
        words_parser_->FindChildren(buffer, block);
      }
      return;
    }

    block->end = Advance(buffer, block->begin);
  }

  LineColumn Advance(const OpenBuffer& buffer, LineColumn position) {
    if (buffer.LineAt(position.line)->size() > position.column) {
      position.column++;
    } else if (buffer.contents()->size() > position.line + 1) {
      position.line++;
      position.column = 0;
    }
    return position;
  }

  LineColumn AdvanceUntilEndOfLine(const OpenBuffer& buffer,
                                   LineColumn position) {
    if (buffer.contents()->size() > position.line + 1) {
      return LineColumn(position.line + 1);
    } else {
      position.column = buffer.LineAt(position.line)->size();
      return position;
    }
  }

  LineColumn AdvanceUntil(const OpenBuffer& buffer,
                          LineColumn position, LineColumn limit,
                          std::function<bool(wchar_t)> predicate) {
    wstring valid = L"abcdefghijklmnopqrstuvwxyz";
    while (!predicate(buffer.character_at(position))) {
      auto old_position = position;
      position = Advance(buffer, position);
      if (position == old_position || position >= limit) {
        return position;
      }
    }
    return position;
  }

  std::unique_ptr<TreeParser> words_parser_ =
      NewWordsTreeParser(
           L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
           NewNullTreeParser());
};

}  // namespace

std::unique_ptr<TreeParser> NewCppTreeParser() {
  return std::unique_ptr<TreeParser>(new CppTreeParser());
}

}  // namespace editor
}  // namespace afc