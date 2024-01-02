#include "src/language/text/mutable_line_sequence.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::infrastructure::Tracker;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;

namespace afc::language::text {
void NullMutableLineSequenceObserver::LinesInserted(LineNumber,
                                                    LineNumberDelta) {}
void NullMutableLineSequenceObserver::LinesErased(LineNumber, LineNumberDelta) {
}
void NullMutableLineSequenceObserver::SplitLine(LineColumn) {}
void NullMutableLineSequenceObserver::FoldedLine(LineColumn) {}
void NullMutableLineSequenceObserver::Sorted() {}
void NullMutableLineSequenceObserver::AppendedToLine(LineColumn) {}
void NullMutableLineSequenceObserver::DeletedCharacters(LineColumn,
                                                        ColumnNumberDelta) {}
void NullMutableLineSequenceObserver::SetCharacter(LineColumn) {}
void NullMutableLineSequenceObserver::InsertedCharacter(LineColumn) {}

MutableLineSequence::MutableLineSequence()
    : MutableLineSequence(
          MakeNonNullShared<NullMutableLineSequenceObserver>()) {}

/* static */ MutableLineSequence MutableLineSequence::WithLine(Line line) {
  MutableLineSequence output;
  output.lines_ = Lines::PushBack(nullptr, std::move(line));
  return output;
}

MutableLineSequence::MutableLineSequence(
    NonNull<std::shared_ptr<MutableLineSequenceObserver>> observer)
    : observer_(std::move(observer)) {}

MutableLineSequence::MutableLineSequence(LineSequence lines)
    : lines_(std::move(lines.lines_)),
      observer_(MakeNonNullShared<NullMutableLineSequenceObserver>()) {}

LineSequence MutableLineSequence::snapshot() const {
  return LineSequence(lines_);
}

LineNumber MutableLineSequence::EndLine() const {
  return LineNumber(0) + size() - LineNumberDelta(1);
}

Range MutableLineSequence::range() const {
  return Range(LineColumn(), LineColumn(EndLine(), back().EndColumn()));
}

NonNull<std::unique_ptr<MutableLineSequence>> MutableLineSequence::copy()
    const {
  NonNull<std::unique_ptr<MutableLineSequence>> output;
  output->lines_ = lines_;
  return output;
}

void MutableLineSequence::insert(
    LineNumber position_line, const LineSequence& source,
    const std::optional<LineModifierSet>& optional_modifiers) {
  CHECK_LE(position_line, EndLine());
  auto prefix = Lines::Prefix(lines_.get_shared(), position_line.read());
  auto suffix = Lines::Suffix(lines_.get_shared(), position_line.read());
  VisitOptional(
      [&prefix, &source](LineModifierSet modifiers) {
        source.ForEach([&](const Line& line) {
          VLOG(6) << "Insert line: " << line.EndColumn()
                  << " modifiers: " << modifiers.size();
          LineBuilder builder(line);
          builder.SetAllModifiers(modifiers);
          prefix =
              Lines::PushBack(std::move(prefix), std::move(builder).Build())
                  .get_shared();
        });
      },
      [&] { prefix = Lines::Append(prefix, source.lines_.get_shared()); },
      optional_modifiers);
  lines_ = VisitPointer(
      Lines::Append(prefix, suffix),
      [](NonNull<Lines::Ptr> value) { return value; },
      [] { return Lines::PushBack(nullptr, Line()); });
  observer_->LinesInserted(position_line, source.size());
}

bool MutableLineSequence::EveryLine(
    const std::function<bool(LineNumber, const Line&)>& callback) const {
  LineNumber line_number;
  return Lines::Every(lines_.get_shared(), [&](const Line& line) {
    return callback(line_number++, line);
  });
}

void MutableLineSequence::ForEach(
    const std::function<void(const Line&)>& callback) const {
  EveryLine([callback](LineNumber, const Line& line) {
    callback(line);
    return true;
  });
}

void MutableLineSequence::ForEach(
    const std::function<void(std::wstring)>& callback) const {
  ForEach([callback](const Line& line) { callback(line.ToString()); });
}

void MutableLineSequence::insert_line(LineNumber line_position, Line line,
                                      ObserverBehavior observer_behavior) {
  LOG(INFO) << "Inserting line at position: " << line_position;
  size_t original_size = lines_->size();
  auto prefix = Lines::Prefix(lines_.get_shared(), line_position.read());
  CHECK_EQ(Lines::Size(prefix), line_position.read());
  auto suffix = Lines::Suffix(lines_.get_shared(), line_position.read());
  CHECK_EQ(Lines::Size(suffix), lines_->size() - line_position.read());
  lines_ = Lines::Append(Lines::PushBack(prefix, std::move(line)), suffix);
  CHECK_EQ(lines_->size(), original_size + 1);
  switch (observer_behavior) {
    case ObserverBehavior::kHide:
      break;
    case ObserverBehavior::kShow:
      observer_->LinesInserted(line_position, LineNumberDelta(1));
  }
}

void MutableLineSequence::set_line(LineNumber position, Line line) {
  static Tracker tracker(L"MutableLineSequence::set_line");
  auto tracker_call = tracker.Call();

  if (position.ToDelta() >= size()) {
    return push_back(line);
  }

  lines_ = lines_->Replace(position.read(), std::move(line));
  // TODO: Why no notify observer_?
}

void MutableLineSequence::DeleteCharactersFromLine(
    LineColumn position, ColumnNumberDelta amount,
    ObserverBehavior observer_behavior) {
  if (amount == ColumnNumberDelta(0)) {
    return;
  }
  CHECK_GT(amount, ColumnNumberDelta(0));
  CHECK_LE(position.column + amount, at(position.line).EndColumn());

  TransformLine(position.line, [&](LineBuilder& options) {
    options.DeleteCharacters(position.column, amount);
  });

  switch (observer_behavior) {
    case ObserverBehavior::kHide:
      break;
    case ObserverBehavior::kShow:
      observer_->DeletedCharacters(position, amount);
  }
}

void MutableLineSequence::DeleteToLineEnd(LineColumn position,
                                          ObserverBehavior observer_behavior) {
  if (position.column < at(position.line).EndColumn()) {
    return DeleteCharactersFromLine(
        position, at(position.line).EndColumn() - position.column,
        observer_behavior);
  }
}

void MutableLineSequence::SetCharacter(LineColumn position, int c,
                                       LineModifierSet modifiers) {
  VLOG(5) << "Set character: " << c << " at " << position
          << " with modifiers: " << modifiers.size();
  TransformLine(position.line, [&](LineBuilder& options) {
    options.SetCharacter(position.column, c, modifiers);
  });

  observer_->SetCharacter(position);
}

void MutableLineSequence::InsertCharacter(LineColumn position) {
  TransformLine(position.line, [&](LineBuilder& options) {
    options.InsertCharacterAtPosition(position.column);
  });
  observer_->InsertedCharacter(position);
}

void MutableLineSequence::AppendToLine(LineNumber line, Line line_to_append,
                                       ObserverBehavior observer_behavior) {
  const LineColumn position = LineColumn(
      std::min(line, EndLine()), at(std::min(line, EndLine())).EndColumn());
  TransformLine(position.line, [&](LineBuilder& options) {
    options.Append(LineBuilder(std::move(line_to_append)));
  });
  switch (observer_behavior) {
    case ObserverBehavior::kHide:
      break;
    case ObserverBehavior::kShow:
      observer_->AppendedToLine(position);
  }
}

void MutableLineSequence::EraseLines(LineNumber first, LineNumber last,
                                     ObserverBehavior observer_behavior) {
  if (first == last) {
    return;  // Optimization to avoid notifying listeners.
  }
  CHECK_LT(first, last);
  CHECK_LE(last, LineNumber(0) + size());
  LOG(INFO) << "Erasing lines in range [" << first << ", " << last << ").";

  lines_ = VisitPointer(
      Lines::Append(Lines::Prefix(lines_.get_shared(), first.read()),
                    Lines::Suffix(lines_.get_shared(), last.read())),
      [](NonNull<Lines::Ptr> value) { return value; },
      [] { return Lines::PushBack(nullptr, Line()); });

  if (observer_behavior == ObserverBehavior::kHide) {
    return;
  }
  observer_->LinesErased(first, last - first);
}

bool MutableLineSequence::MaybeEraseEmptyFirstLine() {
  if (EndLine() == LineNumber(0) || !at(LineNumber()).empty()) return false;
  EraseLines(LineNumber(0), LineNumber(1));
  return true;
}

void MutableLineSequence::SplitLine(LineColumn position) {
  LineBuilder builder(at(position.line));
  builder.DeleteCharacters(ColumnNumber(0), position.column.ToDelta());
  insert_line(position.line + LineNumberDelta(1), std::move(builder).Build(),
              ObserverBehavior::kHide);
  observer_->SplitLine(position);
  DeleteToLineEnd(position, ObserverBehavior::kHide);
}

namespace {
const bool split_line_tests_registration = tests::Register(
    L"MutableLineSequence::SplitLine",
    {
        {.name = L"Normal",
         .callback =
             [] {
               MutableLineSequence contents;
               contents.push_back(L"foo");
               contents.push_back(L"alejandro");
               contents.push_back(L"forero");
               CHECK(contents.snapshot().ToString() ==
                     L"\nfoo\nalejandro\nforero");
               contents.SplitLine(LineColumn(LineNumber(2), ColumnNumber(3)));
               CHECK(contents.snapshot().ToString() ==
                     L"\nfoo\nale\njandro\nforero");
             }},
    });
}

void MutableLineSequence::FoldNextLine(LineNumber position) {
  auto next_line = position + LineNumberDelta(1);
  if (next_line.ToDelta() >= size()) {
    return;
  }

  ColumnNumber initial_size = at(position).EndColumn();
  AppendToLine(position, at(next_line), ObserverBehavior::kHide);
  EraseLines(next_line, position + LineNumberDelta(2), ObserverBehavior::kHide);
  observer_->FoldedLine(LineColumn(position, initial_size));
}

void MutableLineSequence::push_back(std::wstring str) {
  ColumnNumber start;
  for (ColumnNumber i; i < ColumnNumber(str.size()); ++i) {
    wchar_t c = str[i.read()];
    CHECK_GE(i, start);
    if (c == '\n') {
      push_back(Line(str.substr(start.read(), (i - start).read())));
      start = i + ColumnNumberDelta(1);
    }
  }
  push_back(Line(str.substr(start.read())));
}

namespace {
const bool push_back_wstring_tests_registration = tests::Register(
    L"MutableLineSequence::push_back(std::wstring)",
    {
        {.name = L"EmptyInput",
         .callback =
             [] {
               MutableLineSequence contents;
               contents.push_back(L"");
               CHECK(contents.snapshot().ToString() == L"\n");
               CHECK_EQ(contents.EndLine(), LineNumber(1));
             }},
        {.name = L"SingleLine",
         .callback =
             [] {
               MutableLineSequence contents;
               contents.push_back(L"foo");
               CHECK(contents.snapshot().ToString() == L"\nfoo");
               CHECK_EQ(contents.EndLine(), LineNumber(1));
             }},
        {.name = L"MultiLine",
         .callback =
             [] {
               MutableLineSequence contents;
               contents.push_back(L"foo\nbar\nhey\n\n\nquux");
               CHECK(contents.snapshot().ToString() ==
                     L"\nfoo\nbar\nhey\n\n\nquux");
               CHECK_EQ(contents.EndLine(), LineNumber(6));
             }},
    });
}

void MutableLineSequence::push_back(Line line,
                                    ObserverBehavior observer_behavior) {
  LineNumber position = EndLine();
  lines_ = Lines::PushBack(lines_.get_shared(), line);
  switch (observer_behavior) {
    case ObserverBehavior::kHide:
      break;
    case ObserverBehavior::kShow:
      observer_->LinesInserted(position + LineNumberDelta(1),
                               LineNumberDelta(1));
  }
}

void MutableLineSequence::pop_back() {
  EraseLines(LineNumber() + size() - LineNumberDelta(1), LineNumber() + size());
}

LineColumn MutableLineSequence::AdjustLineColumn(LineColumn position) const {
  CHECK_GT(size(), LineNumberDelta(0));
  position.line = std::min(position.line, EndLine());
  position.column = std::min(at(position.line).EndColumn(), position.column);
  return position;
}

std::vector<tests::fuzz::Handler> MutableLineSequence::FuzzHandlers() {
  using namespace tests::fuzz;
  std::vector<Handler> output;

  // Call all our const methods that don't take any arguments.
  output.push_back(Call(std::function<void()>([this]() {
    size();
    EndLine();
    copy();
    back();
    front();
    snapshot().ToString();
    snapshot().CountCharacters();
  })));

  output.push_back(Call(std::function<void(LineNumber, ShortRandomLine)>(
      [this](LineNumber line_number, ShortRandomLine text) {
        line_number = LineNumber(line_number % size());
        insert_line(line_number,
                    LineBuilder{LazyString{std::move(text.value)}}.Build());
      })));

  output.push_back(Call(std::function<void(LineNumber, ShortRandomLine)>(
      [this](LineNumber line_number, ShortRandomLine text) {
        line_number = LineNumber(line_number % size());
        set_line(line_number,
                 LineBuilder{LazyString{std::move(text.value)}}.Build());
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
        a = LineNumber(a % size());
        b = LineNumber(b % size());
        EraseLines(std::min(a, b), std::max(a, b), ObserverBehavior::kShow);
      })));

  output.push_back(
      Call(std::function<void(LineColumn)>([this](LineColumn position) {
        position.line = LineNumber(position.line % size());
        const Line& line = at(position.line);
        if (line.empty()) {
          position.column = ColumnNumber(0);
        } else {
          position.column = ColumnNumber(position.column.ToDelta() %
                                         line.EndColumn().ToDelta());
        }
        SplitLine(position);
      })));

  output.push_back(
      Call(std::function<void(LineNumber)>([this](LineNumber line) {
        static const LineNumberDelta kMargin(10);
        // TODO: Declare a operator% for LineNumber and avoid the roundtrip.
        FoldNextLine(LineNumber(line % (size() + kMargin)));
      })));

  output.push_back(Call(std::function<void(ShortRandomLine)>(
      [this](ShortRandomLine s) { push_back(s.value); })));

  return output;
}

}  // namespace afc::language::text
