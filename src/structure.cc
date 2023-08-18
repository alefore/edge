#include "src/structure.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_contents.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/language/lazy_string/functional.h"
#include "src/operation_scope.h"
#include "src/parse_tree.h"
#include "src/seek.h"
#include "src/tests/tests.h"

namespace afc::editor {
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;

namespace {
LineColumn MoveInRange(Range range, Modifiers modifiers) {
  CHECK_LE(range.begin, range.end);
  return modifiers.direction == Direction::kForwards ? range.end : range.begin;
}

Seek StartSeekToLimit(Structure::SeekInput input) {
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
  return Seek(input.contents, input.position);
}
}  // namespace

Structure* StructureChar() {
  class Impl : public Structure {
   public:
    std::wstring ToString() override { return L"char"; }

    Structure* Lower() override { return StructureChar(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(SeekInput) override {}

    bool SeekToLimit(SeekInput input) override {
      return StartSeekToLimit(input)
                 .WrappingLines()
                 .WithDirection(input.direction)
                 .Once() == Seek::DONE;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range range,
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
    std::wstring ToString() override { return L"word"; }

    Structure* Lower() override { return StructureChar(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kRegion; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(SeekInput input) override {
      Seek(input.contents, input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharIsAlpha();
    }

    bool SeekToLimit(SeekInput input) override {
      StartSeekToLimit(input);
      auto seek = Seek(input.contents, input.position)
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

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range range,
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
    std::wstring ToString() override { return L"symbol"; }

    Structure* Lower() override { return StructureWord(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kRegion; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(SeekInput input) override {
      Seek(input.contents, input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharIn(input.symbol_characters);
    }

    bool SeekToLimit(SeekInput input) override {
      StartSeekToLimit(input);
      return Seek(input.contents, input.position)
                 .WithDirection(input.direction)
                 .WrappingLines()
                 .UntilCurrentCharNotIn(input.symbol_characters) == Seek::DONE;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range range,
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
    std::wstring ToString() override { return L"line"; }

    Structure* Lower() override { return StructureSymbol(); }

    SpaceBehavior space_behavior() override {
      return SpaceBehavior::kBackwards;
    }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(SeekInput input) override {
      switch (input.direction) {
        case Direction::kForwards: {
          auto seek = Seek(input.contents, input.position).WrappingLines();
          if (seek.read() == L'\n') seek.Once();
          return;
        }
        case Direction::kBackwards:
          return;
      }
      LOG(FATAL) << "Invalid direction value.";
    }

    bool SeekToLimit(SeekInput input) override {
      StartSeekToLimit(input);
      switch (input.direction) {
        case Direction::kForwards:
          input.position->column =
              input.contents.at(input.position->line)->EndColumn();
          return true;
        case Direction::kBackwards:
          input.position->column = ColumnNumber(0);
          return Seek(input.contents, input.position)
                     .WrappingLines()
                     .WithDirection(input.direction)
                     .Once() == Seek::DONE;
      }
      LOG(FATAL) << "Invalid direction value.";
      return false;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer& buffer,
                                   LineColumn position, Range,
                                   const Modifiers& modifiers) override {
      int direction = (modifiers.direction == Direction::kBackwards ? -1 : 1);
      size_t repetitions = modifiers.repetitions.value_or(1);
      if (modifiers.direction == Direction::kBackwards &&
          repetitions > position.line.read()) {
        position = LineColumn();
      } else {
        VLOG(5) << "Move: " << position.line << " " << direction << " "
                << repetitions;
        position.line += LineNumberDelta(direction * repetitions);
        if (position.line > buffer.contents().EndLine()) {
          position = LineColumn(buffer.contents().EndLine(),
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
}  // namespace

Structure* StructureMark() {
  class Impl : public Structure {
   public:
    std::wstring ToString() override { return L"mark"; }

    Structure* Lower() override { return StructureLine(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(SeekInput) override {}

    bool SeekToLimit(SeekInput input) override {
      StartSeekToLimit(input);
      // TODO: Implement.
      return true;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer& buffer,
                                   LineColumn position, Range,
                                   const Modifiers& modifiers) override {
      const std::multimap<LineColumn, LineMarks::Mark>& marks =
          buffer.GetLineMarks();
      switch (modifiers.direction) {
        case Direction::kForwards:
          return GetMarkPosition(marks.begin(), marks.end(), position,
                                 modifiers);
          break;
        case Direction::kBackwards:
          return GetMarkPosition(marks.rbegin(), marks.rend(), position,
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

namespace {
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
}

Structure* StructurePage() {
  class Impl : public Structure {
   public:
    std::wstring ToString() override { return L"page"; }

    Structure* Lower() override { return StructureMark(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(SeekInput) override {}

    bool SeekToLimit(SeekInput input) override {
      StartSeekToLimit(input);
      // TODO: Implement.
      return true;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation& scope,
                                   const OpenBuffer& buffer,
                                   LineColumn position, Range range,
                                   const Modifiers& modifiers) override {
      LineNumberDelta lines_to_move = ComputePageMoveLines(
          scope.screen_lines, buffer.Read(buffer_variables::margin_lines_ratio),
          modifiers.repetitions);
      return StructureLine()->Move(scope, buffer, position, range,
                                   {.structure = StructureLine(),
                                    .direction = modifiers.direction,
                                    .repetitions = lines_to_move.read()});
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureSearch() {
  class Impl : public Structure {
   public:
    std::wstring ToString() override { return L"search"; }

    Structure* Lower() override { return StructurePage(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(SeekInput) override {}

    bool SeekToLimit(SeekInput input) override {
      StartSeekToLimit(input);
      // TODO: Implement.
      return true;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range,
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
    std::wstring ToString() override { return L"tree"; }

    Structure* Lower() override { return StructureTree(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(SeekInput input) override {
      Range range;
      if (!FindTreeRange(input.parse_tree, *input.position, input.direction,
                         &range)) {
        return;
      }
      if (!range.Contains(*input.position)) {
        *input.position = range.begin;
      }
    }

    bool SeekToLimit(SeekInput input) override {
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
      return false;
    }

   private:
    bool FindTreeRange(const NonNull<std::shared_ptr<const ParseTree>>& root,
                       LineColumn position, Direction direction,
                       Range* output) {
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

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range range,
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
    std::wstring ToString() override { return L"cursor"; }

    Structure* Lower() override { return StructureSearch(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(SeekInput) override {}

    bool SeekToLimit(SeekInput input) override {
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
        Seek(input.contents, &boundary).WithDirection(input.direction).Once();
      }
      *input.position = boundary;
      return true;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range,
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
    std::wstring ToString() override { return L"sentence"; }

    Structure* Lower() override { return StructureSymbol(); }

    SpaceBehavior space_behavior() override {
      return SpaceBehavior::kBackwards;
    }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(SeekInput input) override {
      Seek(input.contents, input.position)
          .WithDirection(input.direction)
          .WrappingLines()
          .UntilCurrentCharNotIn(spaces);
    }

    bool SeekToLimit(SeekInput input) override {
      StartSeekToLimit(input);
      if (input.direction == Direction::kBackwards) {
        Seek(input.contents, input.position)
            .Backwards()
            .WrappingLines()
            .UntilCurrentCharNotIn(exclamation_signs + spaces);
      }

      while (true) {
        Seek seek(input.contents, input.position);
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

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range,
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
    std::wstring ToString() override { return L"paragraph"; }

    Structure* Lower() override { return StructureLine(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(SeekInput input) override {
      Seek(input.contents, input.position)
          .WithDirection(input.direction)
          .UntilNextLineIsNotSubsetOf(input.line_prefix_characters);
    }

    bool SeekToLimit(SeekInput input) override {
      return StartSeekToLimit(input)
                 .WithDirection(input.direction)
                 .WrappingLines()
                 .UntilNextLineIsSubsetOf(input.line_prefix_characters) ==
             Seek::DONE;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range range,
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
    std::wstring ToString() override { return L"buffer"; }

    Structure* Lower() override { return StructureCursor(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(SeekInput) override {}

    bool SeekToLimit(SeekInput input) override {
      StartSeekToLimit(input);
      if (input.direction == Direction::kBackwards) {
        *input.position = LineColumn();
      } else {
        CHECK_GT(input.contents.size(), LineNumberDelta(0));
        *input.position = input.contents.range().end;
      }
      return false;
    }

    std::optional<LineColumn> Move(const OperationScopeBufferInformation&,
                                   const OpenBuffer&, LineColumn, Range,
                                   const Modifiers&) override {
      return std::nullopt;
    }
  };
  static Impl output;
  return &output;
}
}  // namespace afc::editor
