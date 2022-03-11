#include "src/line_scroll_control.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/char_buffer.h"
#include "src/tests/tests.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
LineScrollControl::Reader::Reader(ConstructorAccessTag,
                                  std::shared_ptr<LineScrollControl> parent)
    : parent_(std::move(parent)) {
  CHECK(parent_ != nullptr);
}

LineScrollControl::LineScrollControl(ConstructorAccessTag, Options options)
    : options_(std::move(options)),
      cursors_([=]() {
        CHECK(options_.buffer != nullptr);
        std::map<LineNumber, std::set<ColumnNumber>> cursors;
        for (auto cursor : *options_.buffer->active_cursors()) {
          cursors[cursor.line].insert(cursor.column);
        }
        return cursors;
      }()),
      range_(GetRange(options_.begin)) {}

std::unique_ptr<LineScrollControl::Reader> LineScrollControl::NewReader() {
  auto output = std::make_unique<LineScrollControl::Reader>(
      Reader::ConstructorAccessTag(), shared_from_this());
  readers_.push_back(output.get());
  return output;
}

std::optional<Range> LineScrollControl::Reader::GetRange() const {
  switch (state_) {
    case State::kDone:
      return std::nullopt;
    case State::kProcessing:
      return parent_->range_;
  }
  LOG(FATAL) << "GetRange didn't handle all cases.";
  return std::nullopt;
}

bool LineScrollControl::Reader::HasActiveCursor() const {
  CHECK(state_ == State::kProcessing);
  return parent_->range_.Contains(parent_->options_.buffer->position());
}

std::set<ColumnNumber> LineScrollControl::Reader::GetCurrentCursors() const {
  CHECK(state_ == State::kProcessing);
  LineNumber line = parent_->range_.begin.line;
  auto it = parent_->cursors_.find(line);
  if (it == parent_->cursors_.end()) {
    return {};
  }
  std::set<ColumnNumber> output;
  for (auto& column : it->second) {
    if (parent_->range_.Contains(LineColumn(line, column))) {
      output.insert(column);
    }
  }
  return output;
}

void LineScrollControl::SignalReaderDone() {
  if (++readers_done_ < readers_.size()) {
    VLOG(8) << "Readers done: " << readers_done_ << " out of "
            << readers_.size();
    return;
  }
  readers_done_ = 0;
  VLOG(6) << "Advancing, finished range: " << range_;
  range_ = GetRange(range_.end);
  VLOG(7) << "Next range: " << range_;

  for (auto& c : readers_) {
    c->state_ = Reader::State::kProcessing;
  }
}

namespace {
ColumnNumber ComputeEndOfColumn(LazyString& line,
                                ColumnNumberDelta output_size) {
  ColumnNumber output;
  ColumnNumberDelta shown;
  while (output.ToDelta() < line.size() && shown < output_size) {
    shown += ColumnNumberDelta(std::max(1, wcwidth(line.get(output))));
    if (shown <= output_size || output.IsZero()) ++output;
  }
  return output;
}

const bool compute_end_of_column_tests_registration = tests::Register(
    L"ComputeEndOfColumn",
    {{.name = L"EmptyAndZero",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*EmptyString(), ColumnNumberDelta())
                      .IsZero());
          }},
     {.name = L"EmptyAndWants",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*EmptyString(), ColumnNumberDelta(80))
                      .IsZero());
          }},
     {.name = L"NormalConsumed",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"alejandro"),
                                     ColumnNumberDelta(80)) == ColumnNumber(9));
          }},
     {.name = L"NormalOverflow",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"alejandro"),
                                     ColumnNumberDelta(6)) == ColumnNumber(6));
          }},
     {.name = L"SimpleWide",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"a🦋lejandro"),
                                     ColumnNumberDelta(6)) == ColumnNumber(5));
          }},
     {.name = L"WideConsumed",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"a🦋o"),
                                     ColumnNumberDelta(6)) == ColumnNumber(3));
          }},
     {.name = L"CharacterDoesNotFit",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"alejo🦋"),
                                     ColumnNumberDelta(6)) == ColumnNumber(5));
          }},
     {.name = L"CharacterAtBorder",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"alejo🦋"),
                                     ColumnNumberDelta(7)) == ColumnNumber(6));
          }},
     {.name = L"SingleWidthNormalCharacter",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"alejo🦋"),
                                     ColumnNumberDelta(1)) == ColumnNumber(1));
          }},
     {.name = L"SingleWidthWide",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"🦋"),
                                     ColumnNumberDelta(1)) == ColumnNumber(1));
          }},
     {.name = L"ManyWideOverflow",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"🦋🦋🦋🦋abcdef"),
                                     ColumnNumberDelta(5)) == ColumnNumber(2));
          }},
     {.name = L"ManyWideOverflowAfter",
      .callback =
          [] {
            CHECK(ComputeEndOfColumn(*NewLazyString(L"🦋🦋🦋🦋abcdef"),
                                     ColumnNumberDelta(10)) == ColumnNumber(6));
          }},
     {.name = L"ManyWideOverflowExact", .callback = [] {
        CHECK(ComputeEndOfColumn(*NewLazyString(L"🦋🦋🦋🦋abcdef"),
                                 ColumnNumberDelta(4)) == ColumnNumber(2));
      }}});
}  // namespace

Range LineScrollControl::GetRange(LineColumn begin) {
  // TODO: This is wrong: it doesn't take into account line filters.
  if (begin.line > options_.buffer->EndLine()) {
    return Range(begin, LineColumn::Max());
  }

  auto line = options_.buffer->LineAt(begin.line);
  if (options_.buffer->Read(buffer_variables::wrap_from_content) &&
      !begin.column.IsZero()) {
    LOG(INFO) << "Skipping spaces (from " << begin << ").";
    while (begin.column < line->EndColumn() &&
           line->get(begin.column) == L' ') {
      begin.column++;
    }
  }

  LineColumn end(
      begin.line,
      begin.column +
          ComputeEndOfColumn(
              *Substring(
                  line->contents(),
                  min(begin.column, ColumnNumber() + line->contents()->size())),
              options_.columns_shown)
              .ToDelta());
  if (end.column < options_.buffer->LineAt(end.line)->EndColumn()) {
    if (options_.buffer->Read(buffer_variables::wrap_from_content)) {
      auto symbols = options_.buffer->Read(buffer_variables::symbol_characters);
      auto line = options_.buffer->LineAt(end.line);
      auto read = [&](ColumnNumber column) { return line->get(column); };
      bool moved = false;
      while (end > begin && symbols.find(read(end.column)) != symbols.npos) {
        --end.column;
        moved = true;
      }
      if (moved) {
        ++end.column;
      }
      if (end.column <= begin.column + ColumnNumberDelta(1)) {
        LOG(INFO) << "Giving up, line exceeds width.";
        end.column = begin.column + options_.columns_shown;
      }
    }
    return Range(begin, end);
  }
  end.line++;
  end.column = ColumnNumber();
  if (end.line > options_.buffer->EndLine()) {
    end = LineColumn::Max();
  }
  return Range(begin, end);
}

}  // namespace editor
}  // namespace afc
