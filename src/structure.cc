#include "src/structure.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_contents.h"
#include "src/buffer_variables.h"
#include "src/seek.h"
#include "src/transformation_delete.h"

namespace afc {
namespace editor {
namespace {
Seek StartSeekToLimit(OpenBuffer* buffer, LineColumn* position) {
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

    void SeekToNext(OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      return StartSeekToLimit(buffer, position)
                 .WrappingLines()
                 .WithDirection(direction)
                 .Once() == Seek::DONE;
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

    void SeekToNext(OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Seek(*buffer->contents(), position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharIsAlpha();
    }

    bool SeekToLimit(OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      auto seek = Seek(*buffer->contents(), position)
                      .WithDirection(direction)
                      .WrappingLines();
      if (direction == FORWARDS &&
          seek.WhileCurrentCharIsUpper() != Seek::DONE) {
        return false;
      }
      if (seek.WhileCurrentCharIsLower() != Seek::DONE) {
        return false;
      }
      if (direction == BACKWARDS && iswupper(seek.read()) &&
          seek.Once() != Seek::DONE) {
        return false;
      }
      return true;
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

    void SeekToNext(OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Seek(*buffer->contents(), position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharIn(
              buffer->Read(buffer_variables::symbol_characters));
    }

    bool SeekToLimit(OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      return Seek(*buffer->contents(), position)
                 .WithDirection(direction)
                 .WrappingLines()
                 .UntilCurrentCharNotIn(buffer->Read(
                     buffer_variables::symbol_characters)) == Seek::DONE;
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

    void SeekToNext(OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      if (direction == FORWARDS) {
        auto seek = Seek(*buffer->contents(), position).WrappingLines();
        if (seek.read() == L'\n') {
          seek.Once();
        }
      }
    }

    bool SeekToLimit(OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      position->column = direction == BACKWARDS
                             ? ColumnNumber(0)
                             : buffer->LineAt(position->line)->EndColumn();
      if (direction == BACKWARDS) {
        return Seek(*buffer->contents(), position)
                   .WrappingLines()
                   .WithDirection(direction)
                   .Once() == Seek::DONE;
      }
      return true;
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureMark() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"mark"; }

    Structure* Lower() override { return StructureLine(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kBuffer; }

    void SeekToNext(OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(OpenBuffer* buffer, Direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      // TODO: Implement.
      return true;
    }
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

    void SeekToNext(OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(OpenBuffer* buffer, Direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      // TODO: Implement.
      return true;
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

    void SeekToNext(OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(OpenBuffer* buffer, Direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      // TODO: Implement.
      return true;
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

    void SeekToNext(OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Range range;
      if (!FindTreeRange(buffer, *position, direction, &range)) {
        return;
      }
      if (!range.Contains(*position)) {
        *position = range.begin;
      }
    }

    bool SeekToLimit(OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      Range range;
      if (!FindTreeRange(buffer, *position, direction, &range)) {
        return false;
      }
      *position = direction == FORWARDS ? range.end : range.begin;
      return true;
    }

   private:
    bool FindTreeRange(OpenBuffer* buffer, LineColumn position,
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
          CHECK_LT(i, tree->children.size());
          if (direction == BACKWARDS) {
            i = tree->children.size() - i - 1;  // From last to first.
          }
          return &tree->children[i];
        };
        while (child < tree->children.size() &&
               (get_child(child)->children.empty() ||
                (direction == FORWARDS
                     ? get_child(child)->range().end <= position
                     : get_child(child)->range().begin > position))) {
          child++;
        }

        if (child < tree->children.size() &&
            (direction == FORWARDS ? tree->range().begin < position
                                   : tree->range().end > position)) {
          tree = get_child(child);
          continue;
        }
        *output = tree->range();
        return true;
      }
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

    void SeekToNext(OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      bool has_boundary = false;
      LineColumn boundary;
      auto cursors = buffer->FindCursors(L"");
      CHECK(cursors != nullptr);
      for (const auto& candidate : *cursors) {
        if (direction == FORWARDS ? (candidate > *position &&
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
      if (direction == BACKWARDS) {
        Seek(*buffer->contents(), &boundary).WithDirection(direction).Once();
      }
      *position = boundary;
      return true;
    }
  };
  static Impl output;
  return &output;
}

Structure* StructureParagraph() {
  class Impl : public Structure {
   public:
    wstring ToString() override { return L"paragraph"; }

    Structure* Lower() override { return StructureLine(); }

    SpaceBehavior space_behavior() override { return SpaceBehavior::kForwards; }

    SearchQuery search_query() override { return SearchQuery::kPrompt; }

    SearchRange search_range() override { return SearchRange::kRegion; }

    void SeekToNext(OpenBuffer* buffer, Direction direction,
                    LineColumn* position) override {
      Seek(*buffer->contents(), position)
          .WithDirection(direction)
          .UntilNextLineIsNotSubsetOf(
              buffer->Read(buffer_variables::line_prefix_characters));
    }

    bool SeekToLimit(OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      return StartSeekToLimit(buffer, position)
                 .WithDirection(direction)
                 .WrappingLines()
                 .UntilNextLineIsSubsetOf(buffer->Read(
                     buffer_variables::line_prefix_characters)) == Seek::DONE;
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

    void SeekToNext(OpenBuffer*, Direction, LineColumn*) override {}

    bool SeekToLimit(OpenBuffer* buffer, Direction direction,
                     LineColumn* position) override {
      StartSeekToLimit(buffer, position);
      if (direction == BACKWARDS) {
        *position = LineColumn();
      } else {
        CHECK_GT(buffer->lines_size(), LineNumberDelta(0));
        position->line = buffer->EndLine();
        position->column = buffer->LineAt(position->line)->EndColumn();
      }
      return false;
    }
  };
  static Impl output;
  return &output;
}

}  // namespace editor
}  // namespace afc
