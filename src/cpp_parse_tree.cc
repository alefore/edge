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
    LineColumn position = root->range.begin;
    int nesting = 0;
    ConsumeBlocksUntilBalanced(
        buffer, root, &nesting, nullptr, nullptr, position, root->range.end,
        true);
    return;
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
        L"std", L"function", L"vector", L"list",
        L"map", L"unordered_map", L"set", L"unordered_set"
        L"int", L"double", L"float", L"string", L"wstring", L"bool", L"char",
        L"size_t",
        // Values
        L"true", L"false", L"nullptr", L"NULL" };
    return tokens.find(str) != tokens.end();
  }

  void ConsumeBlock(const BufferContents& buffer, ParseTree* block,
                    LineColumn limit, int* nesting) {
    VLOG(5) << "Parsing block at position: " << block->range.begin;

    auto c = buffer.character_at(block->range.begin);
    static const wstring id = L"_abcdefghijklmnopqrstuvwxyz";

    if (id.find(tolower(c)) != id.npos) {
      block->range.end = AdvanceUntil(
          buffer, block->range.begin, limit,
          [](wchar_t c) {
            static const wstring id_continuation = id + L"0123456789";
            return id_continuation.find(tolower(c)) == id.npos;
          });
      if (block->range.begin.line == block->range.end.line) {
        auto str = Substring(
                buffer.at(block->range.begin.line)->contents(),
                block->range.begin.column, block->range.end.column
                    - block->range.begin.column)
            ->ToString();
        if (IsReservedToken(str)) {
          block->modifiers.insert(Line::CYAN);
        }
      }
      return;
    }

    if (c == L')' || c == L'}' || c == L']') {
      VLOG(3) << "Unmatched pair closing character.";
      ParseTree child;
      child.range = Range(block->range.begin,
                          Advance(buffer, block->range.begin));
      child.modifiers = {Line::BG_RED, Line::BOLD};
      block->children.push_back(child);
    }

    if (c == L'(' || c == L'{' || c == L'[') {
      ParseTree open_character;
      open_character.range =
          Range(block->range.begin, Advance(buffer, block->range.begin));
      open_character.modifiers = {Line::BG_RED, Line::BOLD};
      CHECK(block->children.empty());
      block->children.push_back(open_character);

      wint_t closing_character;
      switch (c) {
        case L'(': closing_character = L')'; break;
        case L'{': closing_character = L'}'; break;
        case L'[': closing_character = L']'; break;
        default:
          CHECK(false);
      }
      ConsumeBlocksUntilBalanced(buffer, block, nesting, &open_character,
          &closing_character, Advance(buffer, block->range.begin), limit,
          false);
      return;
    }

    if (c == '/' &&
        buffer.character_at(Advance(buffer, block->range.begin)) == '/') {
      block->range.end = AdvanceUntilEndOfLine(buffer, block->range.begin);
      block->modifiers.insert(Line::BLUE);
      words_parser_->FindChildren(buffer, block);
      return;
    }

    if (c == L'"' || c == L'\'') {
      block->range.end = Advance(buffer, block->range.begin);
      while (buffer.character_at(block->range.end) != c &&
             block->range.end < limit) {
        if (buffer.character_at(block->range.end) == '\\') {
          block->range.end = Advance(buffer, block->range.end);
        }
        block->range.end = Advance(buffer, block->range.end);
      }
      if (block->range.end < limit) {
        block->range.end = Advance(buffer, block->range.end);
      }
      if (c == L'\"') {
        words_parser_->FindChildren(buffer, block);
      }
      block->modifiers.insert(Line::YELLOW);
      return;
    }

    if (isdigit(c)) {
      block->range.end = Advance(buffer, block->range.begin);
      while (isdigit(buffer.character_at(block->range.end))) {
        block->range.end = Advance(buffer, block->range.end);
      }
      block->modifiers.insert(Line::YELLOW);
      return;
    }

    block->range.end = Advance(buffer, block->range.begin);
  }

  void ConsumeBlocksUntilBalanced(
      const BufferContents& buffer, ParseTree* block, int* nesting,
      ParseTree* open_character, wint_t* closing_character, LineColumn position,
      LineColumn limit, bool after_newline) {
    while (true) {
      // Skip spaces.
      position = AdvanceUntil(buffer, position, limit,
          [](wchar_t c) { return !iswspace(c) || c == L'\n'; });

      if (position >= limit) {
        block->range.end = limit;
        return;
      }

      auto c = buffer.character_at(position);
      if (c == L'\n') {
        position = Advance(buffer, position);
        after_newline = true;
        continue;
      }

      if (closing_character != nullptr && c == *closing_character) {
        ParseTree tree_end;
        tree_end.range = Range(position, Advance(buffer, position));
        tree_end.modifiers = open_character->modifiers;
        block->children.push_back(tree_end);

        auto modifiers = ModifierForNesting((*nesting)++);
        block->children.front().modifiers = modifiers;
        block->children.back().modifiers = modifiers;

        block->range.end = tree_end.range.end;
        return;
      }

      if (after_newline && c == '#') {
        ParseTree child;
        child.range = Range(position, AdvanceUntilEndOfLine(buffer, position));
        child.modifiers.insert(Line::YELLOW);
        block->children.push_back(child);

        position = child.range.end;
        continue;
      }

      block->children.push_back(ParseTree());
      block->children.back().range.begin = position;
      ConsumeBlock(buffer, &block->children.back(), limit, nesting);
      if (position == block->children.back().range.end) {
        block->range.end = position;
        return;  // Didn't advance.
      }

      CHECK_LT(position, block->children.back().range.end);
      position = block->children.back().range.end;
    }
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
