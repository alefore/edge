#include "cpp_parse_tree.h"

#include <glog/logging.h>

#include "buffer.h"

namespace afc {
namespace editor {
namespace {

class CppTreeParser : public TreeParser {
 public:
  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
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
            child.modifiers.insert(Line::YELLOW);
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
        case '(': case '[': case '{':
        case '"':
          {
            root->children.push_back(ParseTree());
            root->children.back().begin = position;
            int nesting = 0;
            ConsumeBlock(buffer, &root->children.back(), root->end, &nesting);
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
  bool IsReservedToken(const wstring& str) {
    // TODO: Allow the buffer to specify this through a variable.
    static const std::unordered_set<wstring> tokens = {
        L"static", L"extern", L"override", L"virtual",
        L"class", L"struct", L"private", L"public",
        L"using", L"typedef", L"namespace", L"sizeof",
        L"static_cast",
        L"delete", L"new",
        // Flow control.
        L"switch", L"case", L"default",
        L"if", L"else",
        L"for", L"while", L"do",
        L"return",
        // Types
        L"void", L"const", L"auto",
        L"unique_ptr", L"shared_ptr",
        L"std", L"vector", L"list",
        L"int", L"double", L"float", L"string", L"wstring", L"bool", L"char",
        L"size_t",
        // Values
        L"true", L"false", L"nullptr", L"NULL" };
    return tokens.find(str) != tokens.end();
  }

  void ConsumeBlock(const BufferContents& buffer, ParseTree* block,
                    LineColumn limit, int* nesting) {
    LOG(INFO) << "Parsing block at position: " << block->begin;

    auto c = buffer.character_at(block->begin);
    static const wstring id = L"_abcdefghijklmnopqrstuvwxyz";

    if (id.find(tolower(c)) != id.npos) {
      block->end = AdvanceUntil(
          buffer, block->begin, limit,
          [](wchar_t c) {
            static const wstring id_continuation = id + L"0123456789";
            return id_continuation.find(tolower(c)) == id.npos;
          });
      if (block->begin.line == block->end.line) {
        auto str = Substring(buffer.at(block->begin.line)->contents(),
            block->begin.column, block->end.column - block->begin.column)
                ->ToString();
        if (IsReservedToken(str)) {
          block->modifiers.insert(Line::CYAN);
        }
      }
      return;
    }

    if (c == L')' || c == L'}' || c == L']') {
      LOG(INFO) << "Unmatched pair closing character.";
      ParseTree child;
      child.begin = block->begin;
      child.end = Advance(buffer, child.begin);
      child.modifiers = {Line::BG_RED, Line::BOLD};
      block->children.push_back(child);
    }

    if (c == L'(' || c == L'{' || c == L'[') {
      ParseTree open_character;
      open_character.begin = block->begin;
      open_character.end = Advance(buffer, block->begin);
      open_character.modifiers = {Line::BG_RED, Line::BOLD};
      CHECK(block->children.empty());
      block->children.push_back(open_character);

      auto position = Advance(buffer, block->begin);
      wint_t closing_character;
      switch (c) {
        case L'(': closing_character = L')'; break;
        case L'{': closing_character = L'}'; break;
        case L'[': closing_character = L']'; break;
        default:
          CHECK(false);
      }

      while (true) {
        // Skip spaces.
        position = AdvanceUntil(
            buffer, position, limit, [](wchar_t c) { return !iswspace(c); });
        if (position == limit) {
          block->end = limit;
          return;
        }

        if (buffer.character_at(position) == closing_character) {
          ParseTree tree_end;
          tree_end.begin = position;
          tree_end.end = Advance(buffer, position);
          tree_end.modifiers = open_character.modifiers;
          block->children.push_back(tree_end);

          auto modifiers = ModifierForNesting((*nesting)++);
          block->children.front().modifiers = modifiers;
          block->children.back().modifiers = modifiers;

          block->end = tree_end.end;
          return;
        }

        block->children.push_back(ParseTree());
        block->children.back().begin = position;
        ConsumeBlock(buffer, &block->children.back(), limit, nesting);
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
      block->modifiers.insert(Line::BLUE);
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
      block->modifiers.insert(Line::YELLOW);
      return;
    }

    if (isdigit(c)) {
      block->end = Advance(buffer, block->begin);
      while (isdigit(buffer.character_at(block->end))) {
        block->end = Advance(buffer, block->end);
      }
      block->modifiers.insert(Line::YELLOW);
      return;
    }

    block->end = Advance(buffer, block->begin);
  }

  // Return the position immediately after position.
  LineColumn Advance(const BufferContents& buffer, LineColumn position) {
    if (buffer.at(position.line)->size() > position.column) {
      position.column++;
    } else if (buffer.size() > position.line + 1) {
      position.line++;
      position.column = 0;
    }
    return position;
  }

  LineColumn AdvanceUntilEndOfLine(const BufferContents& buffer,
                                   LineColumn position) {
    if (buffer.size() > position.line + 1) {
      return LineColumn(position.line + 1);
    } else {
      position.column = buffer.at(position.line)->size();
      return position;
    }
  }

  std::unordered_set<Line::Modifier, hash<int>> ModifierForNesting(
      int nesting) {
    std::unordered_set<Line::Modifier, hash<int>> output;
    switch (nesting % 5) {
      case 0:
        output.insert(Line::CYAN);
        break;
      case 1:
        output.insert(Line::YELLOW);
        break;
      case 2:
        output.insert(Line::RED);
        break;
      case 3:
        output.insert(Line::BLUE);
        break;
      case 4:
        output.insert(Line::GREEN);
        break;
    }
    if (((nesting / 5) % 2) == 0) {
      output.insert(Line::BOLD);
    }
    return output;
  }

  LineColumn AdvanceUntil(const BufferContents& buffer,
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
