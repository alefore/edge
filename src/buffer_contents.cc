#include "buffer_contents.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "wstring.h"

namespace afc {
namespace editor {

std::unique_ptr<BufferContents> BufferContents::copy() const {
  auto output = std::make_unique<BufferContents>();
  output->lines_ = lines_;
  return output;
}

wint_t BufferContents::character_at(const LineColumn& position) const {
  CHECK_LT(position.line, size());
  auto line = at(position.line);
  return position.column >= line->size() ? L'\n' : line->get(position.column);
}

wstring BufferContents::ToString() const {
  wstring output;
  output.reserve(CountCharacters());
  EveryLine([&output](size_t position, const Line& line) {
    output.append((position == 0 ? L"" : L"\n") + line.ToString());
    return true;
  });
  return output;
}

void BufferContents::insert(size_t position_line, const BufferContents& source,
                            const LineModifierSet* modifiers) {
  if (source.empty()) {
    return;
  }
  CHECK_LT(position_line, size());
  // No need to increment it since it'll move automatically.
  auto insert_position = lines_.begin() + position_line;
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
                            .AddToLine(source.size()));
}

bool BufferContents::EveryLine(
    const std::function<bool(size_t, const Line&)>& callback) const {
  size_t position = 0;
  for (const auto& line : lines_) {
    if (!callback(position++, *line)) {
      return false;
    }
  }
  return true;
}

void BufferContents::ForEach(
    const std::function<void(const Line&)>& callback) const {
  EveryLine([callback](size_t, const Line& line) {
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
    output += line.size() + 1;  // \n.
  });
  if (output > 0) {
    output--;  // Last line has no \n.
  }
  return output;
}

void BufferContents::insert_line(size_t line_position,
                                 shared_ptr<const Line> line) {
  LOG(INFO) << "Inserting line at position: " << line_position;
  lines_.insert(lines_.begin() + line_position, line);
  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithBegin(LineColumn(line_position))
                            .AddToLine(1));
}

void BufferContents::DeleteCharactersFromLine(size_t line, size_t column,
                                              size_t amount) {
  if (amount == 0) {
    return;
  }
  CHECK_LE(column + amount, at(line)->size());

  auto new_line = std::make_shared<Line>(*at(line));
  new_line->DeleteCharacters(column, amount);
  set_line(line, new_line);

  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithBegin(LineColumn(line, column))
                            .WithEnd(LineColumn(line + 1, 0))
                            .AddToColumn(-amount)
                            .OutputColumnGe(column));
}

void BufferContents::DeleteCharactersFromLine(size_t line, size_t column) {
  if (column < at(line)->size()) {
    return DeleteCharactersFromLine(line, column, at(line)->size() - column);
  }
}

void BufferContents::SetCharacter(
    size_t line, size_t column, int c,
    std::unordered_set<LineModifier, hash<int>> modifiers) {
  auto new_line = std::make_shared<Line>(*at(line));
  new_line->SetCharacter(column, c, modifiers);
  set_line(line, new_line);
  NotifyUpdateListeners(CursorsTracker::Transformation());
}

void BufferContents::InsertCharacter(size_t line, size_t column) {
  auto new_line = std::make_shared<Line>(*at(line));
  new_line->InsertCharacterAtPosition(column);
  set_line(line, new_line);
  NotifyUpdateListeners(CursorsTracker::Transformation());
}

void BufferContents::AppendToLine(size_t position, const Line& line_to_append) {
  if (lines_.empty()) {
    push_back(std::make_shared<Line>());
  }
  CHECK(!lines_.empty());
  position = min(position, size() - 1);
  auto line = std::make_shared<Line>(*at(position));
  line->Append(line_to_append);
  set_line(position, line);
  NotifyUpdateListeners(CursorsTracker::Transformation());
}

void BufferContents::EraseLines(size_t first, size_t last) {
  if (first == last) {
    return;  // Optimization to avoid notifying listeners.
  }
  CHECK_LE(first, last);
  CHECK_LE(last, size());
  LOG(INFO) << "Erasing lines in range [" << first << ", " << last << ").";
  lines_.erase(lines_.begin() + first, lines_.begin() + last);
  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithBegin(LineColumn(first))
                            .AddToLine(first - last)
                            .OutputLineGe(first));
}

void BufferContents::SplitLine(LineColumn position) {
  auto tail = std::make_shared<Line>(*at(position.line));
  tail->DeleteCharacters(0, position.column);
  // TODO: Can maybe combine this with next for fewer updates.
  insert_line(position.line + 1, tail);
  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithBegin(position)
                            .WithEnd(LineColumn(position.line + 1, 0))
                            .AddToLine(1)
                            .AddToColumn(-position.column));
  DeleteCharactersFromLine(position.line, position.column);
}

void BufferContents::FoldNextLine(size_t position) {
  if (position + 1 >= size()) {
    return;
  }
  size_t initial_size = at(position)->size();
  // TODO: Can maybe combine this with next for fewer updates.
  AppendToLine(position, *at(position + 1));
  NotifyUpdateListeners(CursorsTracker::Transformation()
                            .WithLineEq(position + 1)
                            .AddToLine(-1)
                            .AddToColumn(initial_size));
  EraseLines(position + 1, position + 2);
}

void BufferContents::push_back(wstring str) {
  return push_back(std::make_shared<Line>(std::move(str)));
}

void BufferContents::AddUpdateListener(
    std::function<void(const CursorsTracker::Transformation&)> listener) {
  CHECK(listener);
  update_listeners_.push_back(listener);
}

void BufferContents::NotifyUpdateListeners(
    const CursorsTracker::Transformation& transformation) {
  for (auto& l : update_listeners_) {
    l(transformation);
  }
}

}  // namespace editor
}  // namespace afc
