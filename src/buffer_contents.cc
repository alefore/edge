#include "src/buffer_contents.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/substring.h"
#include "src/viewers.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

BufferContents::BufferContents() : BufferContents(nullptr) {}

BufferContents::BufferContents(UpdateListener update_listener)
    : update_listener_(update_listener != nullptr
                           ? std::move(update_listener)
                           : [](const CursorsTracker::Transformation&) {}) {
  CHECK(update_listener_ != nullptr);
}

LineNumber BufferContents::EndLine() const {
  return LineNumber(0) + size() - LineNumberDelta(1);
}

Range BufferContents::range() const {
  return Range(LineColumn(), LineColumn(EndLine(), back()->EndColumn()));
}

std::unique_ptr<BufferContents> BufferContents::copy() const {
  auto output = std::make_unique<BufferContents>();
  output->lines_ = lines_;
  return output;
}

wint_t BufferContents::character_at(const LineColumn& position) const {
  CHECK_LE(position.line, EndLine());
  auto line = at(position.line);
  return position.column >= line->EndColumn() ? L'\n'
                                              : line->get(position.column);
}

wstring BufferContents::ToString() const {
  wstring output;
  output.reserve(CountCharacters());
  EveryLine([&output](LineNumber position, const Line& line) {
    output.append((position == LineNumber(0) ? L"" : L"\n") + line.ToString());
    return true;
  });
  return output;
}

void BufferContents::insert(LineNumber position_line,
                            const BufferContents& source,
                            const std::optional<LineModifierSet>& modifiers) {
  CHECK_LE(position_line, EndLine());
  auto prefix = Lines::Prefix(lines_, position_line.line);
  auto suffix = Lines::Suffix(lines_, position_line.line);
  Lines::Every(source.lines_, [&](std::shared_ptr<const Line> line) {
    VLOG(6) << "Insert line: " << line->EndColumn() << " modifiers: "
            << (modifiers.has_value() ? modifiers->size() : -1);
    if (modifiers.has_value()) {
      auto replacement = std::make_shared<Line>(*line);
      replacement->SetAllModifiers(modifiers.value());
      line = replacement;
    }
    prefix = Lines::PushBack(prefix, line);
    return true;
  });
  lines_ = Lines::Append(prefix, suffix);
  update_listener_(CursorsTracker::Transformation()
                       .WithBegin(LineColumn(position_line))
                       .LineDelta(source.size()));
}

bool BufferContents::EveryLine(
    const std::function<bool(LineNumber, const Line&)>& callback) const {
  LineNumber line_number;
  return Lines::Every(lines_, [&](const std::shared_ptr<const Line>& line) {
    return callback(line_number++, *line);
  });
}

void BufferContents::ForEach(
    const std::function<void(const Line&)>& callback) const {
  EveryLine([callback](LineNumber, const Line& line) {
    callback(line);
    return true;
  });
}

void BufferContents::ForEach(
    const std::function<void(wstring)>& callback) const {
  ForEach([callback](const Line& line) { callback(line.ToString()); });
}

size_t BufferContents::CountCharacters() const {
  size_t output = 0;
  ForEach([&output](const Line& line) {
    output += line.EndColumn().ToDelta().column_delta + 1;  // \n.
  });
  if (output > 0) {
    output--;  // Last line has no \n.
  }
  return output;
}

void BufferContents::insert_line(LineNumber line_position,
                                 shared_ptr<const Line> line) {
  LOG(INFO) << "Inserting line at position: " << line_position;
  size_t original_size = Lines::Size(lines_);
  auto prefix = Lines::Prefix(lines_, line_position.line);
  CHECK_EQ(Lines::Size(prefix), line_position.line);
  auto suffix = Lines::Suffix(lines_, line_position.line);
  CHECK_EQ(Lines::Size(suffix), Lines::Size(lines_) - line_position.line);
  lines_ = Lines::Append(Lines::PushBack(prefix, std::move(line)), suffix);
  CHECK_EQ(Lines::Size(lines_), original_size + 1);
  update_listener_(CursorsTracker::Transformation()
                       .WithBegin(LineColumn(line_position))
                       .LineDelta(LineNumberDelta(1)));
}

void BufferContents::set_line(LineNumber position,
                              shared_ptr<const Line> line) {
  static Tracker tracker(L"BufferContents::set_line");
  auto tracker_call = tracker.Call();

  if (position.ToDelta() >= size()) {
    return push_back(line);
  }

  lines_ = lines_->Replace(position.line, std::move(line));
  // TODO: Why no notify update listeners?
}

void BufferContents::DeleteCharactersFromLine(LineColumn position,
                                              ColumnNumberDelta amount) {
  if (amount == ColumnNumberDelta(0)) {
    return;
  }
  CHECK_GT(amount, ColumnNumberDelta(0));
  CHECK_LE(position.column + amount, at(position.line)->EndColumn());

  TransformLine(
      position.line,
      [&](Line::Options* options) {
        options->DeleteCharacters(position.column, amount);
      },
      CursorsTracker::Transformation()
          .WithBegin(position)
          .WithEnd(LineColumn(position.line + LineNumberDelta(1)))
          .ColumnDelta(-amount)
          .ColumnLowerBound(position.column));
}

void BufferContents::DeleteToLineEnd(LineColumn position) {
  if (position.column < at(position.line)->EndColumn()) {
    return DeleteCharactersFromLine(
        position, at(position.line)->EndColumn() - position.column);
  }
}

void BufferContents::SetCharacter(
    LineColumn position, int c,
    std::unordered_set<LineModifier, hash<int>> modifiers) {
  VLOG(5) << "Set character: " << c << " at " << position
          << " with modifiers: " << modifiers.size();
  TransformLine(position.line, [&](Line::Options* options) {
    options->SetCharacter(position.column, c, modifiers);
  });
}

void BufferContents::InsertCharacter(LineColumn position) {
  TransformLine(position.line, [&](Line::Options* options) {
    options->InsertCharacterAtPosition(position.column);
  });
}

void BufferContents::AppendToLine(LineNumber position, Line line_to_append) {
  TransformLine(min(position, LineNumber() + size() - LineNumberDelta(1)),
                [&](Line::Options* options) {
                  options->Append(std::move(line_to_append));
                });
}

void BufferContents::EraseLines(LineNumber first, LineNumber last,
                                CursorsBehavior cursors_behavior) {
  if (first == last) {
    return;  // Optimization to avoid notifying listeners.
  }
  CHECK_LE(first, last);
  CHECK_LE(last, LineNumber(0) + size());
  LOG(INFO) << "Erasing lines in range [" << first << ", " << last << ").";
  lines_ = Lines::Append(Lines::Prefix(lines_, first.line),
                         Lines::Suffix(lines_, last.line));

  if (lines_ == nullptr) {
    lines_ = Lines::PushBack(nullptr, std::make_shared<Line>());
  }

  if (cursors_behavior == CursorsBehavior::kUnmodified) {
    return;
  }
  update_listener_(CursorsTracker::Transformation()
                       .WithBegin(LineColumn(first))
                       .LineDelta(first - last)
                       .LineLowerBound(first));
}

void BufferContents::SplitLine(LineColumn position) {
  // TODO: Can maybe combine this with next for fewer updates.
  insert_line(position.line + LineNumberDelta(1),
              Line::New(Line::Options(*at(position.line))
                            .DeleteCharacters(ColumnNumber(0),
                                              position.column.ToDelta())));
  update_listener_(CursorsTracker::Transformation()
                       .WithBegin(position)
                       .WithEnd(LineColumn(position.line + LineNumberDelta(1)))
                       .LineDelta(LineNumberDelta(1))
                       .ColumnDelta(-position.column.ToDelta()));
  DeleteToLineEnd(position);
}

void BufferContents::FoldNextLine(LineNumber position) {
  auto next_line = position + LineNumberDelta(1);
  if (next_line.ToDelta() >= size()) {
    return;
  }

  ColumnNumberDelta initial_size = at(position)->EndColumn().ToDelta();
  // TODO: Can maybe combine this with next for fewer updates.
  AppendToLine(position, *at(next_line));
  update_listener_(CursorsTracker::Transformation()
                       .WithLineEq(position + LineNumberDelta(1))
                       .LineDelta(LineNumberDelta(-1))
                       .ColumnDelta(initial_size));
  EraseLines(next_line, position + LineNumberDelta(2),
             CursorsBehavior::kAdjust);
}

void BufferContents::push_back(wstring str) {
  return push_back(std::make_shared<Line>(std::move(str)));
}

void BufferContents::push_back(shared_ptr<const Line> line) {
  lines_ = Lines::PushBack(std::move(lines_), line);
  update_listener_({});
}

std::vector<fuzz::Handler> BufferContents::FuzzHandlers() {
  using namespace fuzz;
  std::vector<Handler> output;

  // Call all our const methods that don't take any arguments.
  output.push_back(Call(std::function<void()>([this]() {
    size();
    EndLine();
    copy();
    back();
    front();
    ToString();
    CountCharacters();
  })));

  output.push_back(Call(std::function<void(LineNumber, ShortRandomLine)>(
      [this](LineNumber line_number, ShortRandomLine text) {
        line_number = line_number % size();
        insert_line(line_number, std::make_shared<Line>(Line::Options(
                                     NewLazyString(std::move(text.value)))));
      })));

  output.push_back(Call(std::function<void(LineNumber, ShortRandomLine)>(
      [this](LineNumber line_number, ShortRandomLine text) {
        line_number = line_number % size();
        set_line(line_number, std::make_shared<Line>(Line::Options(
                                  NewLazyString(std::move(text.value)))));
      })));

  // TODO: Call sort.
  // TODO: Call insert.
  // TODO: Call DeleteCharactersFromLine.
  // TODO: Call SetCharacter
  // TODO: Call InsertCharacter
  // TODO: Call AppendToLine.
  // TODO: Call EraseLines

  output.push_back(Call(std::function<void(LineNumber, LineNumber)>(
      [this](LineNumber a, LineNumber b) {
        a = a % size();
        b = b % size();
        EraseLines(min(a, b), max(a, b), CursorsBehavior::kAdjust);
      })));

  output.push_back(
      Call(std::function<void(LineColumn)>([this](LineColumn position) {
        position.line = position.line % size();
        auto line = at(position.line);
        if (line->empty()) {
          position.column = ColumnNumber(0);
        } else {
          position.column = position.column % line->EndColumn().ToDelta();
        }
        SplitLine(position);
      })));

  output.push_back(
      Call(std::function<void(LineNumber)>([this](LineNumber line) {
        static const int kMargin = 10;
        // TODO: Declare a operator% for LineNumber and avoid the roundtrip.
        FoldNextLine(LineNumber(line.line % (Lines::Size(lines_) + kMargin)));
      })));

  output.push_back(Call(std::function<void(ShortRandomLine)>(
      [this](ShortRandomLine s) { push_back(s.value); })));

  return output;
}

}  // namespace editor
}  // namespace afc
