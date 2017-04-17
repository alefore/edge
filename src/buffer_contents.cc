#include "buffer_contents.h"

#include <algorithm>
#include <cassert>
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

class MoveCursors {
 public:
  MoveCursors& WithBegin(LineColumn position) {
    CHECK_EQ(transformation.begin, LineColumn());
    transformation.begin = position;
    return *this;
  }

  MoveCursors& WithEnd(LineColumn position) {
    CHECK_EQ(transformation.end, LineColumn::Max());
    transformation.end = position;
    return *this;
  }

  MoveCursors& WithLineEq(size_t position) {
    transformation.begin.line = position;
    transformation.end.line = position;
    return *this;
  }

  MoveCursors& WithLineGE(size_t position) {
    return WithBegin(LineColumn(position));
  }

  CursorsTracker::Transformation DownBy(size_t delta) {
    return WithCallback([this, delta](LineColumn position) {
      position.line += delta;
      return position;
    });
  }

  CursorsTracker::Transformation WithCallback(
      std::function<LineColumn(LineColumn)> callback) {
    transformation.callback = callback;
    return transformation;
  }

 private:
  CursorsTracker::Transformation transformation;
};

std::unique_ptr<BufferContents> BufferContents::copy() const {
  std::unique_ptr<BufferContents> output(new BufferContents());
  output->lines_ = lines_;
  return output;
}

wint_t BufferContents::character_at(const LineColumn& position) const {
  auto line = at(position.line);
  return position.column >= line->size() ? L'\n' : line->get(position.column);
}

wstring BufferContents::ToString() const {
  wstring output;
  output.reserve(CountCharacters());
  ForEach([&output](size_t position, const Line& line) {
            output.append((position == 0 ? L"" : L"\n") + line.ToString());
            return true;
          });
  return output;
}

void BufferContents::insert(size_t position, const BufferContents& source,
                            size_t first_line, size_t last_line) {
  CHECK_LT(position, size());
  CHECK_LT(first_line, source.size());
  CHECK_LE(first_line, last_line);
  CHECK_LE(last_line, source.size());
  if (first_line == last_line) {
    return;
  }
  lines_.insert(lines_.begin() + position, source.lines_.begin() + first_line,
                source.lines_.begin() + last_line);
  NotifyUpdateListeners(
      MoveCursors().WithLineGE(position).DownBy(last_line - first_line));
}

bool BufferContents::ForEach(
    const std::function<bool(size_t, const Line&)>& callback) const {
  size_t position = 0;
  for (const auto& line : lines_) {
    if (!callback(position++, *line)) { return false; }
  }
  return true;
}

void BufferContents::ForEach(
    const std::function<void(const Line&)>& callback) const {
  ForEach([callback](size_t, const Line& line) {
            callback(line);
            return true;
          });
}

void BufferContents::ForEach(const std::function<void(wstring)>& callback)
    const {
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

void BufferContents::insert_line(
    size_t line_position, shared_ptr<const Line> line) {
  LOG(INFO) << "Inserting line at position: " << line_position;
  lines_.insert(lines_.begin() + line_position, line);
  NotifyUpdateListeners(MoveCursors().WithLineGE(line_position).DownBy(1));
}

void BufferContents::DeleteCharactersFromLine(
    size_t line, size_t column, size_t amount) {
  if (amount == 0) { return; }
  CHECK_LE(column + amount, at(line)->size());

  auto new_line = std::make_shared<Line>(*at(line));
  new_line->DeleteCharacters(column, amount);
  set_line(line, new_line);

  NotifyUpdateListeners(
      MoveCursors()
          .WithBegin(LineColumn(line, column))
          .WithEnd(LineColumn(line, std::numeric_limits<size_t>::max()))
          .WithCallback(
              [column, amount](LineColumn position) {
                CHECK(position.column >= column);
                if (position.column > column + amount) {
                  position.column -= amount;
                } else {
                  position.column = column;
                }
                return position;
              }));
}

void BufferContents::DeleteCharactersFromLine(size_t line, size_t column) {
  return DeleteCharactersFromLine(line, column, at(line)->size() - column);
}

void BufferContents::SetCharacter(size_t line, size_t column, int c,
    std::unordered_set<Line::Modifier, hash<int>> modifiers) {
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

void BufferContents::AppendToLine(
    size_t position, const Line& line_to_append) {
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
  NotifyUpdateListeners(
      MoveCursors()
          .WithLineGE(first)
          .WithCallback(
              [last, first](LineColumn position) {
                CHECK(position.line >= first);
                if (position.line >= last) {
                  position.line -= last - first;
                } else {
                  position.line = first;
                }
                return position;
              }));
}

void BufferContents::SplitLine(size_t line, size_t column) {
  auto tail = std::make_shared<Line>(*at(line));
  tail->DeleteCharacters(0, column);
  // TODO: Can maybe combine this with next for fewer updates.
  insert_line(line + 1, tail);
  NotifyUpdateListeners(
      MoveCursors()
          .WithBegin(LineColumn(line, column))
          .WithEnd(LineColumn(line, std::numeric_limits<size_t>::max()))
          .WithCallback(
              [column](LineColumn position) {
                position.line++;
                position.column -= column;
                return position;
              }));
  DeleteCharactersFromLine(line, column);
}

void BufferContents::FoldNextLine(size_t position) {
  if (position + 1 >= size()) {
    return;
  }
  size_t initial_size = at(position)->size();
  // TODO: Can maybe combine this with next for fewer updates.
  AppendToLine(position, *at(position + 1));
  NotifyUpdateListeners(
      MoveCursors()
          .WithLineEq(position + 1)
          .WithCallback(
              [initial_size](LineColumn cursor) {
                cursor.line--;
                cursor.column += initial_size;
                return cursor;
              }));
  EraseLines(position + 1, position + 2);
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