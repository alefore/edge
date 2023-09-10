#include "src/structure.h"

#include <glog/logging.h>

#include "src/language/lazy_string/functional.h"
#include "src/language/text/line_sequence.h"
#include "src/parse_tree.h"
#include "src/seek.h"

namespace afc::editor {
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumn;
using language::text::LineNumberDelta;
using language::text::Range;

namespace {
Seek StartSeekToLimit(const SeekInput& input) {
  input.position->line =
      std::min(input.contents.EndLine(), input.position->line);
  if (input.position->column >=
      input.contents.at(input.position->line)->EndColumn()) {
    // if (buffer .Read(buffer_variables::extend_lines)) {
    //   MaybeExtendLine(*position);
    //} else {
    input.position->column =
        input.contents.at(input.position->line)->EndColumn();
    //}
  }
  return Seek(input.contents.snapshot(), input.position);
}

bool FindTreeRange(const NonNull<std::shared_ptr<const ParseTree>>& root,
                   LineColumn position, Direction direction, Range* output) {
  NonNull<const ParseTree*> tree = root.get();
  while (true) {
    // Each iteration descends by one level in the parse tree.
    size_t child = 0;
    auto get_child = [=](size_t i) {
      CHECK_LT(i, tree->children().size());
      if (direction == Direction::kBackwards) {
        i = tree->children().size() - i - 1;  // From last to first.
      }
      return NonNull<const ParseTree*>::AddressOf(tree->children()[i]);
    };
    while (child < tree->children().size() &&
           (get_child(child)->children().empty() ||
            (direction == Direction::kForwards
                 ? get_child(child)->range().end <= position
                 : get_child(child)->range().begin > position))) {
      child++;
    }

    if (child < tree->children().size() &&
        (direction == Direction::kForwards ? tree->range().begin < position
                                           : tree->range().end > position)) {
      tree = get_child(child);
      continue;
    }
    *output = tree->range();
    return true;
  }
}
}  // namespace

std::ostream& operator<<(std::ostream& os, const Structure& structure) {
  switch (structure) {
    case Structure::kChar:
      os << "char";
      break;
    case Structure::kWord:
      os << "word";
      break;
    case Structure::kSymbol:
      os << "symbol";
      break;
    case Structure::kLine:
      os << "line";
      break;
    case Structure::kMark:
      os << "mark";
      break;
    case Structure::kPage:
      os << "page";
      break;
    case Structure::kSearch:
      os << "search";
      break;
    case Structure::kTree:
      os << "tree";
      break;
    case Structure::kCursor:
      os << "cursor";
      break;
    case Structure::kSentence:
      os << "sentence";
      break;
    case Structure::kParagraph:
      os << "paragraph";
      break;
    case Structure::kBuffer:
      os << "buffer";
      break;
  }
  return os;
}

std::wstring ToString(Structure structure) {
  std::ostringstream os;
  os << structure;
  return language::FromByteString(os.str());
}

Structure StructureLower(Structure structure) {
  switch (structure) {
    case Structure::kChar:
      return Structure::kChar;
    case Structure::kWord:
      return Structure::kChar;
    case Structure::kSymbol:
      return Structure::kWord;
    case Structure::kLine:
      return Structure::kSymbol;
    case Structure::kMark:
      return Structure::kLine;
    case Structure::kPage:
      return Structure::kMark;
    case Structure::kSearch:
      return Structure::kPage;
    case Structure::kTree:
      return Structure::kTree;
    case Structure::kCursor:
      return Structure::kSearch;
    case Structure::kSentence:
      return Structure::kSymbol;
    case Structure::kParagraph:
      return Structure::kSentence;
    case Structure::kBuffer:
      return Structure::kCursor;
  }
  LOG(FATAL) << "Invalid structure";
  return Structure::kChar;
}

StructureSpaceBehavior GetStructureSpaceBehavior(Structure structure) {
  switch (structure) {
    case Structure::kLine:
    case Structure::kSentence:
      return StructureSpaceBehavior::kBackwards;
    default:
      return StructureSpaceBehavior::kForwards;
  }
}

StructureSearchQuery GetStructureSearchQuery(Structure structure) {
  switch (structure) {
    case Structure::kWord:
    case Structure::kSymbol:
      return StructureSearchQuery::kRegion;
    default:
      return StructureSearchQuery::kPrompt;
  }
}

StructureSearchRange GetStructureSearchRange(Structure structure) {
  switch (structure) {
    case Structure::kLine:
    case Structure::kTree:
    case Structure::kCursor:
    case Structure::kSentence:
    case Structure::kParagraph:
      return StructureSearchRange::kRegion;
    default:
      return StructureSearchRange::kBuffer;
  }
}

namespace {
const std::wstring exclamation_signs = L".?!:";
const std::wstring spaces = L" \n*#";
}  // namespace

void SeekToNext(SeekInput input) {
  switch (input.structure) {
    case Structure::kChar:
    case Structure::kMark:
    case Structure::kPage:
    case Structure::kSearch:
    case Structure::kCursor:
    case Structure::kBuffer:
      return;

    case Structure::kWord:
      Seek(input.contents.snapshot(), input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharIsAlpha();
      return;

    case Structure::kSymbol:
      Seek(input.contents.snapshot(), input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharIn(input.symbol_characters);
      return;

    case Structure::kLine:
      switch (input.direction) {
        case Direction::kForwards: {
          auto seek =
              Seek(input.contents.snapshot(), input.position).WrappingLines();
          if (seek.read() == L'\n') seek.Once();
          return;
        }
        case Direction::kBackwards:
          return;
      }
      LOG(FATAL) << "Invalid direction value.";
      return;

    case Structure::kTree: {
      Range range;
      if (!FindTreeRange(input.parse_tree, *input.position, input.direction,
                         &range)) {
        return;
      }
      if (!range.Contains(*input.position)) {
        *input.position = range.begin;
      }
    }
      return;

    case Structure::kSentence:
      Seek(input.contents.snapshot(), input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharNotIn(spaces);
      return;

    case Structure::kParagraph:
      Seek(input.contents.snapshot(), input.position)
          .WithDirection(input.direction)
          .UntilNextLineIsNotSubsetOf(input.line_prefix_characters);
      return;
  }
}

bool SeekToLimit(SeekInput input) {
  switch (input.structure) {
    case Structure::kChar:
      return StartSeekToLimit(input)
                 .WrappingLines()
                 .WithDirection(input.direction)
                 .Once() == Seek::DONE;

    case Structure::kWord: {
      StartSeekToLimit(input);
      auto seek = Seek(input.contents.snapshot(), input.position)
                      .WithDirection(input.direction)
                      .WrappingLines();
      if (input.direction == Direction::kForwards &&
          seek.WhileCurrentCharIsUpper() != Seek::DONE) {
        return false;
      }
      if (seek.WhileCurrentCharIsLower() != Seek::DONE) {
        return false;
      }
      if (input.direction == Direction::kBackwards && iswupper(seek.read()) &&
          seek.Once() != Seek::DONE) {
        return false;
      }
      return true;
    }

    case Structure::kSymbol:
      StartSeekToLimit(input);
      return Seek(input.contents.snapshot(), input.position)
                 .WithDirection(input.direction)
                 .WrappingLines()
                 .UntilCurrentCharNotIn(input.symbol_characters) == Seek::DONE;

    case Structure::kLine: {
      StartSeekToLimit(input);
      switch (input.direction) {
        case Direction::kForwards:
          input.position->column =
              input.contents.at(input.position->line)->EndColumn();
          return true;
        case Direction::kBackwards:
          input.position->column = ColumnNumber(0);
          return Seek(input.contents.snapshot(), input.position)
                     .WrappingLines()
                     .WithDirection(input.direction)
                     .Once() == Seek::DONE;
      }
      LOG(FATAL) << "Invalid direction value.";
      return false;
    }

    case Structure::kMark:
    case Structure::kPage:
    case Structure::kSearch:
      StartSeekToLimit(input);
      return true;  // TODO: Implement.

    case Structure::kTree: {
      StartSeekToLimit(input);
      Range range;
      if (!FindTreeRange(input.parse_tree, *input.position, input.direction,
                         &range)) {
        return false;
      }
      switch (input.direction) {
        case Direction::kForwards:
          *input.position = range.end;
          return true;
        case Direction::kBackwards:
          *input.position = range.begin;
          return true;
      }
      LOG(FATAL) << "Invalid direction value.";
    }
      return false;
    case Structure::kCursor: {
      StartSeekToLimit(input);
      bool has_boundary = false;
      LineColumn boundary;
      if (input.cursors == nullptr) return false;
      for (const auto& candidate : *input.cursors) {
        if (input.direction == Direction::kForwards
                ? (candidate > *input.position &&
                   (!has_boundary || candidate < boundary))
                : (candidate < *input.position &&
                   (!has_boundary || candidate > boundary))) {
          boundary = candidate;
          has_boundary = true;
        }
      }

      if (!has_boundary) {
        return false;
      }
      if (input.direction == Direction::kBackwards) {
        Seek(input.contents.snapshot(), &boundary)
            .WithDirection(input.direction)
            .Once();
      }
      *input.position = boundary;
    }
      return true;

    case Structure::kSentence: {
      StartSeekToLimit(input);
      if (input.direction == Direction::kBackwards) {
        Seek(input.contents.snapshot(), input.position)
            .Backwards()
            .WrappingLines()
            .UntilCurrentCharNotIn(exclamation_signs + spaces);
      }

      while (true) {
        Seek seek(input.contents.snapshot(), input.position);
        seek.WithDirection(input.direction);
        if (seek.UntilCurrentCharIn(exclamation_signs) == Seek::DONE) {
          if (input.direction == Direction::kForwards) {
            return seek.UntilCurrentCharNotIn(exclamation_signs) == Seek::DONE;
          }
          return seek.WithDirection(Direction::kForwards)
                     .WrappingLines()
                     .UntilNextCharNotIn(spaces + exclamation_signs) ==
                 Seek::DONE;
        }
        if (seek.ToNextLine() == Seek::UNABLE_TO_ADVANCE) {
          return false;
        }
        if (input.contents.at(input.position->line)->EndColumn() ==
            ColumnNumber(0)) {
          if (input.direction == Direction::kForwards) {
            return false;
          }
          return seek.WithDirection(Direction::kForwards)
                     .WrappingLines()
                     .UntilNextCharNotIn(spaces + exclamation_signs) ==
                 Seek::DONE;
        }
      }
    }

    case Structure::kParagraph:
      return StartSeekToLimit(input)
                 .WithDirection(input.direction)
                 .WrappingLines()
                 .UntilNextLineIsSubsetOf(input.line_prefix_characters) ==
             Seek::DONE;

    case Structure::kBuffer:
      StartSeekToLimit(input);
      if (input.direction == Direction::kBackwards) {
        *input.position = LineColumn();
      } else {
        CHECK_GT(input.contents.size(), LineNumberDelta(0));
        *input.position = input.contents.range().end;
      }
      return false;
  }
  LOG(FATAL) << "Invalid structure or case didn't return: " << input.structure;
  return false;
}
}  // namespace afc::editor
