#include "src/structure.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_contents.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/lazy_string_functional.h"
#include "src/parse_tree.h"
#include "src/seek.h"
#include "src/transformation/delete.h"

namespace afc::editor {
namespace {
LineColumn MoveInRange(Range range, Modifiers modifiers) {
  CHECK_LE(range.begin, range.end);
  return modifiers.direction == Direction::kForwards ? range.end : range.begin;
}

// Arguments:
//   prefix_len: The length of prefix that we skip when calls is 0.
//   suffix_start: The position where the suffix starts. This is the base when
//       calls is 2.
//   elements: The total number of elements.
//   direction: The direction of movement.
//   repetitions: The nth element to jump to.
//   structure_range: The StructureRange. If FROM_CURRENT_POSITION_TO_END, it
//       reverses the direction.
//   calls: The number of consecutive number of times this command has run.
size_t ComputePosition(size_t prefix_len, size_t suffix_start, size_t elements,
                       Direction direction, size_t repetitions, size_t calls) {
  CHECK_LE(prefix_len, suffix_start);
  CHECK_LE(suffix_start, elements);
  if (calls > 1) {
    return ComputePosition(prefix_len, suffix_start, elements,
                           ReverseDirection(direction), repetitions, calls - 2);
  }
  if (calls == 1) {
    return ComputePosition(0, elements, elements, direction, repetitions, 0);
  }

  if (direction == Direction::kForwards) {
    return min(prefix_len + repetitions - 1, elements);
  } else {
    return suffix_start - min(suffix_start, repetitions - 1);
  }
}

Seek StartSeekToLimit(const OpenBuffer* buffer, LineColumn* position) {
  CHECK_GT(buffer->lines_size(), LineNumberDelta(0));
  position->line = std::min(buffer->EndLine(), position->line);
  if (position->column >= buffer->LineAt(position->line)->EndColumn()) {
    // if (buffer->Read(buffer_variables::extend_lines)) {
    //   MaybeExtendLine(*position);
    //} else {
    position->column = buffer->LineAt(position->line)->EndColumn();
    //}
  }
  return Seek(*buffer->contents(), position);
}
}  // namespace

Structure* StructureChar() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"char"; }

    Structure* Lower() override { return StructureChar(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(const OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      return StartSeekToLimit(buffer, position)
                 .WrappingLines()
                 .WithDirection(direction)
                 .Once() == Seek::DONE;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer* buffer,
                                                  const Modifiers& modifiers,
                                                  LineColumn position,
                                                  int calls) override {
      const wstring& line_prefix_characters =
          buffer->Read(buffer_variables::line_prefix_characters);
      const auto& line = buffer->LineAt(position.line);
      if (line == nullptr) return std::nullopt;
      ColumnNumber start =
          FindFirstColumnWithPredicate(*line->contents(), [&](ColumnNumber,
                                                              wchar_t c) {
            return line_prefix_characters.find(c) == string::npos;
          }).value_or(line->EndColumn());
      ColumnNumber end = line->EndColumn();
      while (start + ColumnNumberDelta(1) < end &&
             (line_prefix_characters.find(
                  line->get(end - ColumnNumberDelta(1))) != string::npos)) {
        end--;
      }
      position.column = ColumnNumber(ComputePosition(
          start.column, end.column, line->EndColumn().column,
          modifiers.direction, modifiers.repetitions.value_or(1), calls));
      CHECK_LE(position.column, line->EndColumn());
      return position;
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range range,
                                   const Modifiers& modifiers) override {
      return MoveInRange(range, modifiers);
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureWord() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"word"; }

    Structure* Lower() override { return StructureChar(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kRegion; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(const OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Seek(*buffer->contents(), position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharIsAlpha();
    }

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      auto seek = Seek(*buffer->contents(), position)
                      .WithDirection(direction)
                      .WrappingLines();
      if (direction == Direction::kForwards &&
          seek.WhileCurrentCharIsUpper() != Seek::DONE) {
        return false;
      }
      if (seek.WhileCurrentCharIsLower() != Seek::DONE) {
        return false;
      }
      if (direction == Direction::kBackwards && iswupper(seek.read()) &&
          seek.Once() != Seek::DONE) {
        return false;
      }
      return true;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer*,
                                                  const Modifiers&, LineColumn,
                                                  int) override {
      return std::nullopt;  // TODO: Implement.
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range range,
                                   const Modifiers& modifiers) override {
      return MoveInRange(range, modifiers);
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureSymbol() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"symbol"; }

    Structure* Lower() override { return StructureWord(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kRegion; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(const OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Seek(*buffer->contents(), position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharIn(
              buffer->Read(buffer_variables::symbol_characters));
    }

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      return Seek(*buffer->contents(), position)
                 .WithDirection(direction)
                 .WrappingLines()
                 .UntilCurrentCharNotIn(buffer->Read(
                     buffer_variables::symbol_characters)) == Seek::DONE;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer* buffer,
                                                  const Modifiers& modifiers,
                                                  LineColumn position,
                                                  int) override {
      position.column = modifiers.direction == Direction::kBackwards
                            ? buffer->LineAt(position.line)->EndColumn()
                            : ColumnNumber();

      VLOG(4) << "Start SYMBOL GotoCommand: " << modifiers;
      Range range = buffer->FindPartialRange(modifiers, position);
      switch (modifiers.direction) {
        case Direction::kForwards: {
          Modifiers modifiers_copy = modifiers;
          modifiers_copy.repetitions = 1;
          range = buffer->FindPartialRange(modifiers_copy,
                                           buffer->PositionBefore(range.end));
          position = range.begin;
        } break;

        case Direction::kBackwards: {
          Modifiers modifiers_copy = modifiers;
          modifiers_copy.repetitions = 1;
          modifiers_copy.direction = Direction::kForwards;
          range = buffer->FindPartialRange(modifiers_copy, range.begin);
          position = buffer->PositionBefore(range.end);
        } break;
      }
      return position;
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range range,
                                   const Modifiers& modifiers) override {
      return MoveInRange(range, modifiers);
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureLine() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"line"; }

    Structure* Lower() override { return StructureSymbol(); }

    SpaceBehavior space_behavior() override {
      return SpaceBehavior::kBackwards;
    }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(const OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      switch (direction) {
        case Direction::kForwards: {
          auto seek = Seek(*buffer->contents(), position).WrappingLines();
          if (seek.read() == L'\n') {
            seek.Once();
          }
          return;
        }
        case Direction::kBackwards:
          return;
      }
      LOG(FATAL) << "Invalid direction value.";
    }

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      switch (direction) {
        case Direction::kForwards:
          position->column = buffer->LineAt(position->line)->EndColumn();
          return true;
        case Direction::kBackwards:
          position->column = ColumnNumber(0);
          return Seek(*buffer->contents(), position)
                     .WrappingLines()
                     .WithDirection(direction)
                     .Once() == Seek::DONE;
      }
      LOG(FATAL) << "Invalid direction value.";
      return false;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer* buffer,
                                                  const Modifiers& modifiers,
                                                  LineColumn position,
                                                  int calls) override {
      size_t lines = buffer->EndLine().line;
      position.line =
          LineNumber(ComputePosition(0, lines, lines, modifiers.direction,
                                     modifiers.repetitions.value_or(1), calls));
      CHECK_LE(position.line, LineNumber(0) + buffer->contents()->size());
      return position;
    }

    std::optional<LineColumn> Move(const OpenBuffer& buffer,
                                   LineColumn position, Range,
                                   const Modifiers& modifiers) override {
      int direction = (modifiers.direction == Direction::kBackwards ? -1 : 1);
      size_t repetitions = modifiers.repetitions.value_or(1);
      if (modifiers.direction == Direction::kBackwards &&
          repetitions > position.line.line) {
        position = LineColumn();
      } else {
        position.line += LineNumberDelta(direction * repetitions);
        if (position.line > buffer.contents()->EndLine()) {
          position = LineColumn(buffer.contents()->EndLine(),
                                std::numeric_limits<ColumnNumber>::max());
        }
      }
      return position;
    }
  };
  static Impl output;
  return &output;
}

namespace {
template <typename Iterator>
static LineColumn GetMarkPosition(Iterator it_begin, Iterator it_end,
                                  LineColumn current,
                                  const Modifiers& modifiers) {
  using P = pair<const size_t, LineMarks::Mark>;
  Iterator it = std::upper_bound(
      it_begin, it_end, P(current.line.line, LineMarks::Mark()),
      modifiers.direction == Direction::kForwards
          ? [](const P& a, const P& b) { return a.first < b.first; }
          : [](const P& a, const P& b) { return a.first > b.first; });
  if (it == it_end) {
    return current;
  }

  for (size_t i = 1; i < modifiers.repetitions; i++) {
    size_t position = it->first;
    ++it;
    // Skip more marks for the same line.
    while (it != it_end && it->first == position) {
      ++it;
    }
    if (it == it_end) {
      // Can't move past the current mark.
      return LineColumn(LineNumber(position));
    }
  }

  return it->second.target;
}
}  // namespace

Structure* StructureMark() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"mark"; }

    Structure* Lower() override { return StructureLine(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(const OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(const OpenBuffer* buffer, Direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      // TODO: Implement.
      return true;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer* buffer,
                                                  const Modifiers& modifiers,
                                                  LineColumn position,
                                                  int calls) override {
      // Navigates marks in the current buffer.
      const std::multimap<size_t, LineMarks::Mark>* marks =
          buffer->GetLineMarks();
      std::vector<pair<size_t, LineMarks::Mark>> lines;
      std::unique_copy(marks->begin(), marks->end(), std::back_inserter(lines),
                       [](const pair<size_t, LineMarks::Mark>& entry1,
                          const pair<size_t, LineMarks::Mark>& entry2) {
                         return (entry1.first == entry2.first);
                       });
      size_t index =
          ComputePosition(0, lines.size(), lines.size(), modifiers.direction,
                          modifiers.repetitions.value_or(1), calls);
      CHECK_LE(index, lines.size());
      position.line = LineNumber(lines.at(index).first);
      return position;
    }

    std::optional<LineColumn> Move(const OpenBuffer& buffer,
                                   LineColumn position, Range,
                                   const Modifiers& modifiers) override {
      const multimap<size_t, LineMarks::Mark>* marks = buffer.GetLineMarks();
      switch (modifiers.direction) {
        case Direction::kForwards:
          return GetMarkPosition(marks->begin(), marks->end(), position,
                                 modifiers);
          break;
        case Direction::kBackwards:
          return GetMarkPosition(marks->rbegin(), marks->rend(), position,
                                 modifiers);
      }
      CHECK(false);
      return std::nullopt;
    }

   private:
  };
  static Impl output;
  return &output;
}

Structure* StructurePage() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"page"; }

    Structure* Lower() override { return StructureMark(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(const OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(const OpenBuffer* buffer, Direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      // TODO: Implement.
      return true;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer* buffer,
                                                  const Modifiers& modifiers,
                                                  LineColumn position,
                                                  int calls) override {
      CHECK_GT(buffer->contents()->size(), LineNumberDelta(0));
      auto view_size = buffer->viewers()->view_size();
      auto lines = view_size.has_value() ? view_size->line : LineNumberDelta(1);
      size_t pages =
          ceil(static_cast<double>(buffer->contents()->size().line_delta) /
               lines.line_delta);
      position.line =
          LineNumber(0) +
          lines * ComputePosition(0, pages, pages, modifiers.direction,
                                  modifiers.repetitions.value_or(1), calls);
      CHECK_LT(position.line.ToDelta(), buffer->contents()->size());
      return position;
    }

    std::optional<LineColumn> Move(const OpenBuffer& buffer,
                                   LineColumn position, Range range,
                                   const Modifiers& modifiers) override {
      static const auto kDefaultScreenLines = LineNumberDelta(24);
      auto view_size = buffer.viewers()->view_size();
      auto screen_lines =
          max(0.2,
              1.0 - 2.0 * buffer.Read(buffer_variables::margin_lines_ratio)) *
          (view_size.has_value() ? view_size->line : kDefaultScreenLines);
      auto lines =
          modifiers.repetitions.value_or(1) * screen_lines - LineNumberDelta(1);
      return StructureLine()->Move(buffer, position, range,
                                   {.structure = StructureLine(),
                                    .direction = modifiers.direction,
                                    .repetitions = lines.line_delta});
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureSearch() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"search"; }

    Structure* Lower() override { return StructurePage(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(const OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(const OpenBuffer* buffer, Direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      // TODO: Implement.
      return true;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer*,
                                                  const Modifiers&, LineColumn,
                                                  int) override {
      return std::nullopt;  // TODO: Implement.
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range,
                                   const Modifiers&) override {
      return std::nullopt;
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureTree() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"tree"; }

    Structure* Lower() override { return StructureTree(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(const OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Range range;
      if (!FindTreeRange(buffer, *position, direction, &range)) {
        return;
      }
      if (!range.Contains(*position)) {
        *position = range.begin;
      }
    }

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      Range range;
      if (!FindTreeRange(buffer, *position, direction, &range)) {
        return false;
      }
      switch (direction) {
        case Direction::kForwards:
          *position = range.end;
          return true;
        case Direction::kBackwards:
          *position = range.begin;
          return true;
      }
      LOG(FATAL) << "Invalid direction value.";
      return false;
    }

   private:
    bool FindTreeRange(const OpenBuffer* buffer, LineColumn position,
                       Direction direction, Range* output) {
      auto root = buffer->parse_tree();
      if (root == nullptr) {
        return false;
      }

      const ParseTree* tree = root.get();
      while (true) {
        // Each iteration descends by one level in the parse tree.
        size_t child = 0;
        auto get_child = [=](size_t i) {
          CHECK_LT(i, tree->children().size());
          if (direction == Direction::kBackwards) {
            i = tree->children().size() - i - 1;  // From last to first.
          }
          return &tree->children()[i];
        };
        while (child < tree->children().size() &&
               (get_child(child)->children().empty() ||
                (direction == Direction::kForwards
                     ? get_child(child)->range().end <= position
                     : get_child(child)->range().begin > position))) {
          child++;
        }

        if (child < tree->children().size() &&
            (direction == Direction::kForwards
                 ? tree->range().begin < position
                 : tree->range().end > position)) {
          tree = get_child(child);
          continue;
        }
        *output = tree->range();
        return true;
      }
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer*,
                                                  const Modifiers&, LineColumn,
                                                  int) override {
      return std::nullopt;  // TODO: Implement.
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range range,
                                   const Modifiers& modifiers) override {
      return MoveInRange(range, modifiers);
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureCursor() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"cursor"; }

    Structure* Lower() override { return StructureSearch(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(const OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      bool has_boundary = false;
      LineColumn boundary;
      auto cursors = buffer->FindCursors(L"");
      if (cursors == nullptr) return false;
      for (const auto& candidate : *cursors) {
        if (direction == Direction::kForwards
                ? (candidate > *position &&
                   (!has_boundary || candidate < boundary))
                : (candidate < *position &&
                   (!has_boundary || candidate > boundary))) {
          boundary = candidate;
          has_boundary = true;
        }
      }

      if (!has_boundary) {
        return false;
      }
      if (direction == Direction::kBackwards) {
        Seek(*buffer->contents(), &boundary).WithDirection(direction).Once();
      }
      *position = boundary;
      return true;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer*,
                                                  const Modifiers&, LineColumn,
                                                  int) override {
      // TODO: Implement.
#if 0
      auto buffer = editor_state->current_buffer()->second;
      auto cursors = buffer->active_cursors();
      auto modifiers = editor_state->modifiers();
      CursorsSet::iterator current = buffer->current_cursor();
      for (size_t i = 0;
           i < modifiers.repetitions.value_or(1) && current != cursors->begin();
           i++) {
        --current;
      }
#endif
      return std::nullopt;
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range,
                                   const Modifiers&) override {
      return std::nullopt;
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureSentence() {
  static const std::wstring exclamation_signs = L".?!:";
  static const std::wstring spaces = L" \n*#";
  // The exclamation signs at the end are considered part of the sentence.
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"sentence"; }

    Structure* Lower() override { return StructureSymbol(); }

    SpaceBehavior space_behavior() override {
      return SpaceBehavior::kBackwards;
    }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(const OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Seek(*buffer->contents(), position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharNotIn(spaces);
    }

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      if (direction == Direction::kBackwards) {
        Seek(*buffer->contents(), position)
            .Backwards()
            .WrappingLines()
            .UntilCurrentCharNotIn(exclamation_signs + spaces);
      }

      while (true) {
        Seek seek(*buffer->contents(), position);
        seek.WithDirection(direction);
        if (seek.UntilCurrentCharIn(exclamation_signs) == Seek::DONE) {
          if (direction == Direction::kForwards) {
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
        if (buffer->contents()->at(position->line)->EndColumn() ==
            ColumnNumber(0)) {
          if (direction == Direction::kForwards) {
            return false;
          }
          return seek.WithDirection(Direction::kForwards)
                     .WrappingLines()
                     .UntilNextCharNotIn(spaces + exclamation_signs) ==
                 Seek::DONE;
        }
      }
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer*,
                                                  const Modifiers&, LineColumn,
                                                  int) override {
      return std::nullopt;  // TODO: Implement.
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range,
                                   const Modifiers&) override {
      return std::nullopt;
    }
  };
  static Impl output;
  return &output;
};

Structure* StructureParagraph() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"paragraph"; }

    Structure* Lower() override { return StructureLine(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(const OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Seek(*buffer->contents(), position)
          .WithDirection(direction)
          .UntilNextLineIsNotSubsetOf(
              buffer->Read(buffer_variables::line_prefix_characters));
    }

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      return StartSeekToLimit(buffer, position)
                 .WithDirection(direction)
                 .WrappingLines()
                 .UntilNextLineIsSubsetOf(buffer->Read(
                     buffer_variables::line_prefix_characters)) == Seek::DONE;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer*,
                                                  const Modifiers&, LineColumn,
                                                  int) override {
      return std::nullopt;  // TODO: Implement.
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range range,
                                   const Modifiers& modifiers) override {
      return MoveInRange(range, modifiers);
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureBuffer() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"buffer"; }

    Structure* Lower() override { return StructureCursor(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(const OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(const OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      if (direction == Direction::kBackwards) {
        *position = LineColumn();
      } else {
        CHECK_GT(buffer->lines_size(), LineNumberDelta(0));
        position->line = buffer->EndLine();
        position->column = buffer->LineAt(position->line)->EndColumn();
      }
      return false;
    }

    std::optional<LineColumn> ComputeGoToPosition(const OpenBuffer*,
                                                  const Modifiers&, LineColumn,
                                                  int) override {
      return std::nullopt;  // TODO: Implement.
    }

    std::optional<LineColumn> Move(const OpenBuffer&, LineColumn, Range,
                                   const Modifiers&) override {
      return std::nullopt;
    }
  };
  static Impl output;
  return &output;
}
}  // namespace afc::editor
