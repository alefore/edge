#include <glog/logging.h>

#include "src/buffer_contents.h"
#include "src/direction.h"
#include "src/language/lazy_string/functional.h"
#include "src/modifiers.h"
#include "src/operation_scope_buffer_information.h"
#include "src/structure.h"
#include "src/tests/tests.h"

namespace afc::editor {
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::Range;

namespace {
LineColumn MoveInRange(Range range, Modifiers modifiers) {
  CHECK_LE(range.begin, range.end);
  return modifiers.direction == Direction::kForwards ? range.end : range.begin;
}

template <typename Iterator>
static LineColumn GetMarkPosition(Iterator it_begin, Iterator it_end,
                                  LineColumn current,
                                  const Modifiers& modifiers) {
  using P = std::pair<const LineColumn, LineMarks::Mark>;
  Iterator it = std::upper_bound(
      it_begin, it_end,
      P(LineColumn(current.line),
        LineMarks::Mark{.source_line = LineNumber(),
                        .target_line_column = LineColumn()}),
      modifiers.direction == Direction::kForwards
          ? [](const P& a, const P& b) { return a.first < b.first; }
          : [](const P& a, const P& b) { return a.first > b.first; });
  if (it == it_end) {
    return current;
  }

  for (size_t i = 1; i < modifiers.repetitions; i++) {
    LineColumn position = it->first;
    ++it;
    // Skip more marks for the same line.
    while (it != it_end && it->first == position) {
      ++it;
    }
    if (it == it_end) {
      // Can't move past the current mark.
      return position;
    }
  }

  return it->second.target_line_column;
}

LineNumberDelta ComputePageMoveLines(
    std::optional<LineNumberDelta> view_size_lines, double margin_lines_ratio,
    std::optional<size_t> repetitions) {
  static const auto kDefaultScreenLines = LineNumberDelta(24);
  const LineNumberDelta screen_lines(
      std::max(0.2, 1.0 - 2.0 * margin_lines_ratio) *
      static_cast<double>(
          (view_size_lines ? *view_size_lines : kDefaultScreenLines).read()));
  return repetitions.value_or(1) * screen_lines - LineNumberDelta(1);
}

bool compute_page_move_lines_test_registration = tests::Register(
    L"ComputePageMoveLines",
    std::vector<tests::Test>(
        {{.name = L"Simple",
          .callback =
              [] {
                CHECK_EQ(ComputePageMoveLines(LineNumberDelta(10), 0.2, 1),
                         LineNumberDelta(5));
              }},
         {.name = L"Large", .callback = [] {
            CHECK_EQ(ComputePageMoveLines(LineNumberDelta(100), 0.1, 5),
                     LineNumberDelta(399));
          }}}));
}  // namespace

std::optional<LineColumn> Move(
    const OperationScopeBufferInformation& buffer_information,
    Structure structure, const BufferContents& contents, LineColumn position,
    Range range, const Modifiers& modifiers) {
  switch (structure) {
    case Structure::kChar:
    case Structure::kWord:
    case Structure::kSymbol:
    case Structure::kTree:
    case Structure::kParagraph:
      return MoveInRange(range, modifiers);

    case Structure::kLine: {
      int direction = (modifiers.direction == Direction::kBackwards ? -1 : 1);
      size_t repetitions = modifiers.repetitions.value_or(1);
      if (modifiers.direction == Direction::kBackwards &&
          repetitions > position.line.read()) {
        position = LineColumn();
      } else {
        VLOG(5) << "Move: " << position.line << " " << direction << " "
                << repetitions;
        position.line += LineNumberDelta(direction * repetitions);
        if (position.line > contents.EndLine()) {
          position = LineColumn(contents.EndLine(),
                                std::numeric_limits<ColumnNumber>::max());
        }
      }
      return position;
    }

    case Structure::kMark: {
      switch (modifiers.direction) {
        case Direction::kForwards:
          return GetMarkPosition(buffer_information.line_marks.begin(),
                                 buffer_information.line_marks.end(), position,
                                 modifiers);
          break;
        case Direction::kBackwards:
          return GetMarkPosition(buffer_information.line_marks.rbegin(),
                                 buffer_information.line_marks.rend(), position,
                                 modifiers);
      }
      CHECK(false);
      return std::nullopt;
    }

    case Structure::kPage:
      return Move(
          buffer_information, Structure::kLine, contents, position, range,
          {.structure = Structure::kLine,
           .direction = modifiers.direction,
           .repetitions =
               ComputePageMoveLines(buffer_information.screen_lines,
                                    buffer_information.margin_lines_ratio,
                                    modifiers.repetitions)
                   .read()});
    case Structure::kSearch:
    case Structure::kCursor:
    case Structure::kSentence:
    case Structure::kBuffer:
      return std::nullopt;
  }
  LOG(FATAL) << "Invalid structure or case didn't return: " << structure;
  return std::nullopt;
}
}  // namespace afc::editor
