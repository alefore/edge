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

LineNumber BufferContents::EndLine() const {
  return LineNumber(0) + size() - LineNumberDelta(1);
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
                            const LineModifierSet* modifiers) {
  CHECK_LE(position_line, EndLine());
  // No need to increment it since it'll move automatically.
  auto insert_position = lines_.begin() + position_line.line;
  for (auto line : source.lines_) {
    if (modifiers != nullptr) {
      auto replacement = std::make_shared<Line>(*line);
      replacement->SetAllModifiers(*modifiers);
      line = replacement;
    }
    lines_.insert(insert_position, line);
  }
  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithBegin(LineColumn(position_line))
                            .LineDelta(source.size()));
}

bool BufferContents::EveryLine(
    const std::function<bool(LineNumber, const Line&)>& callback) const {
  LineNumber line_number;
  for (const auto& line : lines_) {
    if (!callback(line_number++, *line)) {
      return false;
    }
  }
  return true;
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
  lines_.insert(lines_.begin() + line_position.line, line);
  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithBegin(LineColumn(line_position))
                            .LineDelta(LineNumberDelta(1)));
}

void BufferContents::set_line(LineNumber position,
                              shared_ptr<const Line> line) {
  if (position.ToDelta() >= size()) {
    return push_back(line);
  }

  lines_[position.line] = line;
}

void BufferContents::DeleteCharactersFromLine(LineNumber line,
                                              ColumnNumber column,
                                              ColumnNumberDelta amount) {
  if (amount == ColumnNumberDelta(0)) {
    return;
  }
  CHECK_GT(amount, ColumnNumberDelta(0));
  CHECK_LE(column + amount, at(line)->EndColumn());

  Line::Options options(*at(line));
  options.DeleteCharacters(column, amount);
  set_line(line, std::make_shared<Line>(options));

  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithBegin(LineColumn(line, column))
                            .WithEnd(LineColumn(line + LineNumberDelta(1)))
                            .ColumnDelta(-amount)
                            .ColumnLowerBound(column));
}

void BufferContents::DeleteCharactersFromLine(LineNumber line,
                                              ColumnNumber column) {
  if (column < at(line)->EndColumn()) {
    return DeleteCharactersFromLine(line, column,
                                    at(line)->EndColumn() - column);
  }
}

void BufferContents::SetCharacter(
    LineNumber line, ColumnNumber column, int c,
    std::unordered_set<LineModifier, hash<int>> modifiers) {
  CHECK_LE(line, EndLine());
  auto new_line = std::make_shared<Line>(*at(line));
  new_line->SetCharacter(column, c, modifiers);
  set_line(line, new_line);
  NotifyUpdateListeners(CursorsTracker::Transformation());
}

void BufferContents::InsertCharacter(LineNumber line, ColumnNumber column) {
  auto new_line = std::make_shared<Line>(*at(line));
  new_line->InsertCharacterAtPosition(column);
  set_line(line, new_line);
  NotifyUpdateListeners(CursorsTracker::Transformation());
}

void BufferContents::AppendToLine(LineNumber position, Line line_to_append) {
  if (lines_.empty()) {
    push_back(std::make_shared<Line>());
  }
  CHECK(!lines_.empty());
  position = min(position, LineNumber() + size() - LineNumberDelta(1));
  Line::Options options(*at(position));
  options.Append(std::move(line_to_append));
  set_line(position, std::make_shared<Line>(std::move(options)));
  NotifyUpdateListeners(CursorsTracker::Transformation());
}

void BufferContents::EraseLines(LineNumber first, LineNumber last,
                                CursorsBehavior cursors_behavior) {
  if (first == last) {
    return;  // Optimization to avoid notifying listeners.
  }
  CHECK_LE(first, last);
  CHECK_LE(last, LineNumber(0) + size());
  LOG(INFO) << "Erasing lines in range [" << first << ", " << last << ").";
  lines_.erase(lines_.begin() + first.line, lines_.begin() + last.line);
  if (lines_.empty()) {
    lines_.insert(lines_.begin(), std::make_shared<Line>());
  }

  if (cursors_behavior == CursorsBehavior::kUnmodified) {
    return;
  }
  NotifyUpdateListeners(CursorsTracker::Transformation()
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
  NotifyUpdateListeners(
      CursorsTracker::Transformation()
          .WithBegin(position)
          .WithEnd(LineColumn(position.line + LineNumberDelta(1)))
          .LineDelta(LineNumberDelta(1))
          .ColumnDelta(-position.column.ToDelta()));
  DeleteCharactersFromLine(position.line, position.column);
}

void BufferContents::FoldNextLine(LineNumber position) {
  auto next_line = position + LineNumberDelta(1);
  if (next_line >= LineNumber(0) + size()) {
    return;
  }
  ColumnNumberDelta initial_size = at(position)->EndColumn().ToDelta();
  // TODO: Can maybe combine this with next for fewer updates.
  AppendToLine(position, *at(next_line));
  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithLineEq(position + LineNumberDelta(1))
                            .LineDelta(LineNumberDelta(-1))
                            .ColumnDelta(initial_size));
  EraseLines(next_line, position + LineNumberDelta(2),
             CursorsBehavior::kAdjust);
}

void BufferContents::push_back(wstring str) {
  return push_back(std::make_shared<Line>(std::move(str)));
}

void BufferContents::AddUpdateListener(
    std::function<void(const CursorsTracker::Transformation&)> listener) {
  CHECK(listener);
  update_listeners_.push_back(listener);
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
        FoldNextLine(LineNumber(line.line % (lines_.size() + kMargin)));
      })));

  output.push_back(Call(std::function<void(ShortRandomLine)>(
      [this](ShortRandomLine s) { push_back(s.value); })));

  output.push_back(Call(std::function<void()>([this]() {
    AddUpdateListener([](const CursorsTracker::Transformation&) {});
  })));

  return output;
}

void BufferContents::NotifyUpdateListeners(
    const CursorsTracker::Transformation& transformation) {
  for (auto& l : update_listeners_) {
    l(transformation);
  }
}

}  // namespace editor
}  // namespace afc
