#include "src/structure.h"

#include <glog/logging.h>

#include "src/language/lazy_string/functional.h"
#include "src/language/text/line_sequence.h"
#include "src/parse_tree.h"
#include "src/seek.h"

namespace container = afc::language::container;

using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::text::LineColumn;
using afc::language::text::LineNumberDelta;
using afc::language::text::Range;

namespace afc::editor {

namespace {
Seek StartSeekToLimit(const SeekInput& input) {
  input.position->line =
      std::min(input.contents.EndLine(), input.position->line);
  if (input.position->column >=
      input.contents.at(input.position->line).EndColumn()) {
    // if (buffer .Read(buffer_variables::extend_lines)) {
    //   MaybeExtendLine(*position);
    //} else {
    input.position->column =
        input.contents.at(input.position->line).EndColumn();
    //}
  }
  return Seek(input.contents, input.position);
}

bool FindTreeRange(const NonNull<std::shared_ptr<const ParseTree>>& root,
                   LineColumn position, Direction direction, Range* output) {
  NonNull<const ParseTree*> tree = root.get();
  while (true) {
    // Each iteration descends by one level in the parse tree.
    size_t child = 0;
    auto get_child = [=](size_t i) {
      CHECK_LT(i, tree->children().size());
      if (direction == Direction::Backwards) {
        i = tree->children().size() - i - 1;  // From last to first.
      }
      return NonNull<const ParseTree*>::AddressOf(tree->children()[i]);
    };
    while (child < tree->children().size() &&
           (get_child(child)->children().empty() ||
            (direction == Direction::Forwards
                 ? get_child(child)->range().end() <= position
                 : get_child(child)->range().begin() > position))) {
      child++;
    }

    if (child < tree->children().size() &&
        (direction == Direction::Forwards ? tree->range().begin() < position
                                           : tree->range().end() > position)) {
      tree = get_child(child);
      continue;
    }
    *output = tree->range();
    return true;
  }
}
}  // namespace

std::ostream& operator<<(std::ostream& os, const Structure& structure) {
  os << ToNonEmptySingleLine(structure);
  return os;
}

language::lazy_string::NonEmptySingleLine ToNonEmptySingleLine(
    const Structure& structure) {
  switch (structure) {
    case Structure::Char:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"char")};
      break;
    case Structure::Word:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"word")};
      break;
    case Structure::Symbol:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"symbol")};
      break;
    case Structure::Line:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"line")};
      break;
    case Structure::Mark:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"mark")};
      break;
    case Structure::Page:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"page")};
      break;
    case Structure::Search:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"search")};
      break;
    case Structure::Tree:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"tree")};
      break;
    case Structure::Cursor:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"cursor")};
      break;
    case Structure::Sentence:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"sentence")};
      break;
    case Structure::Paragraph:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"paragraph")};
      break;
    case Structure::Buffer:
      return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"buffer")};
      break;
  }
  LOG(FATAL) << "Invalid structure";
  return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"invalid")};
}

Structure StructureLower(Structure structure) {
  switch (structure) {
    case Structure::Char:
      return Structure::Char;
    case Structure::Word:
      return Structure::Char;
    case Structure::Symbol:
      return Structure::Word;
    case Structure::Line:
      return Structure::Symbol;
    case Structure::Mark:
      return Structure::Line;
    case Structure::Page:
      return Structure::Mark;
    case Structure::Search:
      return Structure::Page;
    case Structure::Tree:
      return Structure::Tree;
    case Structure::Cursor:
      return Structure::Search;
    case Structure::Sentence:
      return Structure::Symbol;
    case Structure::Paragraph:
      return Structure::Sentence;
    case Structure::Buffer:
      return Structure::Cursor;
  }
  LOG(FATAL) << "Invalid structure";
  return Structure::Char;
}

StructureSpaceBehavior GetStructureSpaceBehavior(Structure structure) {
  switch (structure) {
    case Structure::Line:
    case Structure::Sentence:
      return StructureSpaceBehavior::kBackwards;
    default:
      return StructureSpaceBehavior::kForwards;
  }
}

StructureSearchQuery GetStructureSearchQuery(Structure structure) {
  switch (structure) {
    case Structure::Word:
    case Structure::Symbol:
      return StructureSearchQuery::Region;
    default:
      return StructureSearchQuery::Prompt;
  }
}

StructureSearchRange GetStructureSearchRange(Structure structure) {
  switch (structure) {
    case Structure::Line:
    case Structure::Tree:
    case Structure::Cursor:
    case Structure::Sentence:
    case Structure::Paragraph:
      return StructureSearchRange::Region;
    default:
      return StructureSearchRange::Buffer;
  }
}

namespace {
const std::unordered_set<wchar_t> exclamation_signs =
    container::MaterializeUnorderedSet(std::wstring_view{L".?!:"});
const std::unordered_set<wchar_t> spaces =
    container::MaterializeUnorderedSet(std::wstring_view{L" \n*#"});
const std::unordered_set<wchar_t> kSpacesAndExclamationSigns =
    container::MaterializeUnorderedSet(std::array{spaces, exclamation_signs} |
                                       std::ranges::views::join);
}  // namespace

void SeekToNext(SeekInput input) {
  switch (input.structure) {
    case Structure::Char:
    case Structure::Mark:
    case Structure::Page:
    case Structure::Search:
    case Structure::Cursor:
    case Structure::Buffer:
      return;

    case Structure::Word:
      Seek(input.contents, input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharIsAlpha();
      return;

    case Structure::Symbol:
      Seek(input.contents, input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharIn(input.symbol_characters);
      return;

    case Structure::Line:
      switch (input.direction) {
        case Direction::Forwards: {
          Seek seek(input.contents, input.position);
          seek.WrappingLines();
          if (seek.read() == L'\n') seek.Once();
          return;
        }
        case Direction::Backwards:
          return;
      }
      LOG(FATAL) << "Invalid direction value.";
      return;

    case Structure::Tree: {
      Range range;
      if (!FindTreeRange(input.parse_tree, *input.position, input.direction,
                         &range)) {
        return;
      }
      if (!range.Contains(*input.position)) {
        *input.position = range.begin();
      }
    }
      return;

    case Structure::Sentence:
      Seek(input.contents, input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharNotIn(spaces);
      return;

    case Structure::Paragraph:
      Seek(input.contents, input.position)
          .WithDirection(input.direction)
          .UntilNextLineIsNotSubsetOf(input.line_prefix_characters);
      return;
  }
}

bool SeekToLimit(SeekInput input) {
  switch (input.structure) {
    case Structure::Char:
      return StartSeekToLimit(input)
                 .WrappingLines()
                 .WithDirection(input.direction)
                 .Once() == Seek::Result::Done;

    case Structure::Word: {
      StartSeekToLimit(input);
      Seek seek(input.contents, input.position);
      seek.WithDirection(input.direction).WrappingLines();
      if (input.direction == Direction::Forwards &&
          seek.WhileCurrentCharIsUpper() != Seek::Result::Done) {
        return false;
      }
      if (seek.WhileCurrentCharIsLower() != Seek::Result::Done) {
        return false;
      }
      if (input.direction == Direction::Backwards && iswupper(seek.read()) &&
          seek.Once() != Seek::Result::Done) {
        return false;
      }
      return true;
    }

    case Structure::Symbol:
      StartSeekToLimit(input);
      return Seek(input.contents, input.position)
                 .WithDirection(input.direction)
                 .WrappingLines()
                 .UntilCurrentCharNotIn(input.symbol_characters) ==
             Seek::Result::Done;

    case Structure::Line: {
      StartSeekToLimit(input);
      switch (input.direction) {
        case Direction::Forwards:
          input.position->column =
              input.contents.at(input.position->line).EndColumn();
          return true;
        case Direction::Backwards:
          input.position->column = ColumnNumber(0);
          return Seek(input.contents, input.position)
                     .WrappingLines()
                     .WithDirection(input.direction)
                     .Once() == Seek::Result::Done;
      }
      LOG(FATAL) << "Invalid direction value.";
      return false;
    }

    case Structure::Mark:
    case Structure::Page:
    case Structure::Search:
      StartSeekToLimit(input);
      return true;  // TODO: Implement.

    case Structure::Tree: {
      StartSeekToLimit(input);
      Range range;
      if (!FindTreeRange(input.parse_tree, *input.position, input.direction,
                         &range)) {
        return false;
      }
      switch (input.direction) {
        case Direction::Forwards:
          *input.position = range.end();
          return true;
        case Direction::Backwards:
          *input.position = range.begin();
          return true;
      }
      LOG(FATAL) << "Invalid direction value.";
    }
      return false;
    case Structure::Cursor: {
      StartSeekToLimit(input);
      bool has_boundary = false;
      LineColumn boundary;
      if (input.cursors == nullptr) return false;
      for (const auto& candidate : *input.cursors) {
        if (input.direction == Direction::Forwards
                ? (candidate > *input.position &&
                   (!has_boundary || candidate < boundary))
                : (candidate < *input.position &&
                   (!has_boundary || candidate > boundary))) {
          boundary = candidate;
          has_boundary = true;
        }
      }

      if (!has_boundary) return false;
      if (input.direction == Direction::Backwards) {
        Seek(input.contents, &boundary).WithDirection(input.direction).Once();
      }
      *input.position = boundary;
    }
      return true;

    case Structure::Sentence: {
      StartSeekToLimit(input);
      if (input.direction == Direction::Backwards) {
        Seek(input.contents, input.position)
            .Backwards()
            .WrappingLines()
            .UntilCurrentCharNotIn(kSpacesAndExclamationSigns);
      }

      while (true) {
        Seek seek(input.contents, input.position);
        seek.WithDirection(input.direction);
        if (seek.UntilCurrentCharIn(exclamation_signs) == Seek::Result::Done) {
          if (input.direction == Direction::Forwards) {
            return seek.UntilCurrentCharNotIn(exclamation_signs) ==
                   Seek::Result::Done;
          }
          return seek.WithDirection(Direction::Forwards)
                     .WrappingLines()
                     .UntilNextCharNotIn(kSpacesAndExclamationSigns) ==
                 Seek::Result::Done;
        }
        if (seek.ToNextLine() == Seek::Result::UnableToAdvance) {
          return false;
        }
        if (input.contents.at(input.position->line).EndColumn() ==
            ColumnNumber(0)) {
          if (input.direction == Direction::Forwards) {
            return false;
          }
          return seek.WithDirection(Direction::Forwards)
                     .WrappingLines()
                     .UntilNextCharNotIn(kSpacesAndExclamationSigns) ==
                 Seek::Result::Done;
        }
      }
    }

    case Structure::Paragraph:
      return StartSeekToLimit(input)
                 .WithDirection(input.direction)
                 .WrappingLines()
                 .UntilNextLineIsSubsetOf(input.line_prefix_characters) ==
             Seek::Result::Done;

    case Structure::Buffer:
      StartSeekToLimit(input);
      if (input.direction == Direction::Backwards) {
        *input.position = LineColumn();
      } else {
        CHECK_GT(input.contents.size(), LineNumberDelta(0));
        *input.position = input.contents.range().end();
      }
      return false;
  }
  LOG(FATAL) << "Invalid structure or case didn't return: " << input.structure;
  return false;
}
}  // namespace afc::editor
