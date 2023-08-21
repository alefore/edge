#include "src/line.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/padding.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/line_with_cursor.h"
#include "src/tests/tests.h"

namespace afc {
namespace editor {
namespace lazy_string = language::lazy_string;

using ::operator<<;

using infrastructure::Tracker;
using language::compute_hash;
using language::Error;
using language::MakeHashableIteratorRange;
using language::MakeNonNullShared;
using language::NonNull;
using lazy_string::ColumnNumber;
using lazy_string::ColumnNumberDelta;
using lazy_string::LazyString;
using lazy_string::NewLazyString;

namespace {
const bool line_tests_registration = tests::Register(
    L"LineTests",
    {{.name = L"ConstructionOfEmptySetsEndOfLine",
      .callback =
          [] {
            LineBuilder options;
            options.insert_end_of_line_modifiers({LineModifier::kRed});
            Line line(std::move(options));
            CHECK(line.end_of_line_modifiers().find(LineModifier::kRed) !=
                  line.end_of_line_modifiers().end());
          }},
     {.name = L"CopyOfEmptyPreservesEndOfLine",
      .callback =
          [] {
            LineBuilder options;
            options.insert_end_of_line_modifiers({LineModifier::kRed});
            Line initial_line(std::move(options));
            Line final_line(std::move(initial_line));
            CHECK(final_line.end_of_line_modifiers().find(LineModifier::kRed) !=
                  final_line.end_of_line_modifiers().end());
          }},
     {.name = L"EndOfLineModifiersChangesHash",
      .callback =
          [] {
            LineBuilder options;
            size_t initial_hash = std::hash<Line>{}(Line(options));
            options.insert_end_of_line_modifiers({LineModifier::kRed});
            size_t final_hash = std::hash<Line>{}(Line(options));
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"ContentChangesHash",
      .callback =
          [] {
            CHECK(
                std::hash<Line>{}(Line(LineBuilder(NewLazyString(L"alejo")))) !=
                std::hash<Line>{}(Line(LineBuilder(NewLazyString(L"Xlejo")))));
          }},
     {.name = L"ModifiersChangesHash",
      .callback =
          [] {
            LineBuilder options(NewLazyString(L"alejo"));
            size_t initial_hash = std::hash<Line>{}(Line(options));
            options.modifiers[ColumnNumber(2)].insert(LineModifier::kRed);
            size_t final_hash = std::hash<Line>{}(Line(options));
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"MetadataBecomesAvailable", .callback = [] {
        futures::Future<NonNull<std::shared_ptr<LazyString>>> future;
        Line line(LineBuilder().SetMetadata(
            LineMetadataEntry{.initial_value = NewLazyString(L"Foo"),
                              .value = std::move(future.value)}));
        CHECK(line.metadata()->ToString() == L"Foo");
        future.consumer(NewLazyString(L"Bar"));
        CHECK(line.metadata()->ToString() == L"Bar");
      }}});
}

ColumnNumber LineBuilder::EndColumn() const {
  // TODO: Compute this separately, taking the width of characters into account.
  return ColumnNumber(0) + contents->size();
}

void LineBuilder::SetCharacter(ColumnNumber column, int c,
                               const LineModifierSet& c_modifiers) {
  ValidateInvariants();
  VLOG(4) << "Start SetCharacter: " << column;
  auto str = NewLazyString(std::wstring(1, c));
  if (column >= EndColumn()) {
    column = EndColumn();
    contents = lazy_string::Append(std::move(contents), std::move(str));
  } else {
    contents = lazy_string::Append(
        lazy_string::Substring(std::move(contents), ColumnNumber(0),
                               column.ToDelta()),
        std::move(str),
        lazy_string::Substring(contents, column + ColumnNumberDelta(1)));
  }

  metadata_ = std::nullopt;

  // Return the modifiers that are effective at a given position.
  auto Read = [&](ColumnNumber position) -> LineModifierSet {
    if (modifiers.empty() || modifiers.begin()->first > position) return {};
    auto it = modifiers.lower_bound(position);
    if (it == modifiers.end() || it->first > position) --it;
    return it->second;
  };

  auto Set = [&](ColumnNumber position, LineModifierSet value) {
    LineModifierSet previous_value =
        position.IsZero() ? LineModifierSet{}
                          : Read(position - ColumnNumberDelta(1));
    if (previous_value == value)
      modifiers.erase(position);
    else
      modifiers[position] = value;
  };

  ColumnNumber after_column = column + ColumnNumberDelta(1);
  LineModifierSet modifiers_after_column = Read(after_column);

  Set(column, c_modifiers);

  if (after_column < EndColumn()) Set(after_column, modifiers_after_column);

  ValidateInvariants();

  for (std::pair<ColumnNumber, LineModifierSet> entry : modifiers)
    VLOG(5) << "Modifiers: " << entry.first << ": " << entry.second;
}

namespace {
const bool line_set_character_tests_registration = tests::Register(
    L"LineTestsSetCharacter",
    {{.name = L"ConsecutiveSets", .callback = [] {
        LineBuilder options;
        options.AppendString(std::wstring(L"ALEJANDRO"), std::nullopt);
        CHECK(options.contents->ToString() == L"ALEJANDRO");
        CHECK(options.modifiers.empty());

        options.SetCharacter(ColumnNumber(1), L'l',
                             LineModifierSet{LineModifier::kBold});
        CHECK(options.contents->ToString() == L"AlEJANDRO");
        CHECK_EQ(options.modifiers.size(), 2ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers.find(ColumnNumber(2))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(2), L'e',
                             LineModifierSet{LineModifier::kBold});
        CHECK(options.contents->ToString() == L"AleJANDRO");
        CHECK_EQ(options.modifiers.size(), 2ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers.find(ColumnNumber(3))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(3), L'j',
                             LineModifierSet{LineModifier::kUnderline});
        CHECK(options.contents->ToString() == L"AlejANDRO");
        CHECK_EQ(options.modifiers.size(), 3ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers.find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers.find(ColumnNumber(4))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(5), L'n',
                             LineModifierSet{LineModifier::kBlue});
        CHECK(options.contents->ToString() == L"AlejAnDRO");
        CHECK_EQ(options.modifiers.size(), 5ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers.find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers.find(ColumnNumber(4))->second,
                 LineModifierSet{});
        CHECK_EQ(options.modifiers.find(ColumnNumber(5))->second,
                 LineModifierSet{LineModifier::kBlue});
        CHECK_EQ(options.modifiers.find(ColumnNumber(6))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(4), L'a',
                             LineModifierSet{LineModifier::kRed});
        CHECK(options.contents->ToString() == L"AlejanDRO");
        CHECK_EQ(options.modifiers.size(), 5ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers.find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers.find(ColumnNumber(4))->second,
                 LineModifierSet{LineModifier::kRed});
        CHECK_EQ(options.modifiers.find(ColumnNumber(5))->second,
                 LineModifierSet{LineModifier::kBlue});
        CHECK_EQ(options.modifiers.find(ColumnNumber(6))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(0), L'a',
                             LineModifierSet{LineModifier::kBold});
        CHECK(options.contents->ToString() == L"alejanDRO");
        CHECK_EQ(options.modifiers.size(), 5ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(0))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers.find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers.find(ColumnNumber(4))->second,
                 LineModifierSet{LineModifier::kRed});
        CHECK_EQ(options.modifiers.find(ColumnNumber(5))->second,
                 LineModifierSet{LineModifier::kBlue});
        CHECK_EQ(options.modifiers.find(ColumnNumber(6))->second,
                 LineModifierSet{});
      }}});
}  // namespace

void LineBuilder::InsertCharacterAtPosition(ColumnNumber column) {
  ValidateInvariants();
  contents = lazy_string::Append(
      lazy_string::Substring(contents, ColumnNumber(0), column.ToDelta()),
      NewLazyString(L" "), lazy_string::Substring(contents, column));

  std::map<ColumnNumber, LineModifierSet> new_modifiers;
  for (auto& m : modifiers) {
    new_modifiers[m.first + (m.first < column ? ColumnNumberDelta(0)
                                              : ColumnNumberDelta(1))] =
        std::move(m.second);
  }
  modifiers = std::move(new_modifiers);

  metadata_ = std::nullopt;
  ValidateInvariants();
}

void LineBuilder::AppendCharacter(wchar_t c, LineModifierSet modifier) {
  ValidateInvariants();
  CHECK(modifier.find(LineModifier::kReset) == modifier.end());
  modifiers[ColumnNumber(0) + contents->size()] = modifier;
  contents = lazy_string::Append(std::move(contents),
                                 NewLazyString(std::wstring(1, c)));
  metadata_ = std::nullopt;
  ValidateInvariants();
}

void LineBuilder::AppendString(NonNull<std::shared_ptr<LazyString>> suffix) {
  AppendString(std::move(suffix), std::nullopt);
}

void LineBuilder::AppendString(
    NonNull<std::shared_ptr<LazyString>> suffix,
    std::optional<LineModifierSet> suffix_modifiers) {
  ValidateInvariants();
  LineBuilder suffix_line(std::move(suffix));
  if (suffix_modifiers.has_value() &&
      suffix_line.contents->size() > ColumnNumberDelta(0)) {
    suffix_line.modifiers[ColumnNumber(0)] = suffix_modifiers.value();
  }
  Append(std::move(suffix_line));
  ValidateInvariants();
}

void LineBuilder::AppendString(
    std::wstring suffix, std::optional<LineModifierSet> suffix_modifiers) {
  AppendString(NewLazyString(std::move(suffix)), std::move(suffix_modifiers));
}

void LineBuilder::Append(LineBuilder line) {
  ValidateInvariants();
  end_of_line_modifiers_ = std::move(line.end_of_line_modifiers_);
  if (line.EndColumn().IsZero()) return;
  ColumnNumberDelta original_length = EndColumn().ToDelta();
  contents = lazy_string::Append(std::move(contents), std::move(line.contents));
  metadata_ = std::nullopt;

  auto initial_modifier =
      line.modifiers.empty() || line.modifiers.begin()->first != ColumnNumber(0)
          ? LineModifierSet{}
          : line.modifiers.begin()->second;
  auto final_modifier =
      modifiers.empty() ? LineModifierSet{} : modifiers.rbegin()->second;
  if (initial_modifier != final_modifier) {
    modifiers[ColumnNumber() + original_length] = initial_modifier;
  }
  for (auto& [position, new_modifiers] : line.modifiers) {
    if ((modifiers.empty() ? LineModifierSet{} : modifiers.rbegin()->second) !=
        new_modifiers) {
      modifiers[position + original_length] = std::move(new_modifiers);
    }
  }

  end_of_line_modifiers_ = line.end_of_line_modifiers_;

  ValidateInvariants();
}

void LineBuilder::SetBufferLineColumn(BufferLineColumn buffer_line_column) {
  buffer_line_column_ = buffer_line_column;
}
std::optional<BufferLineColumn> LineBuilder::buffer_line_column() const {
  return buffer_line_column_;
}

LineBuilder& LineBuilder::SetMetadata(
    std::optional<LineMetadataEntry> metadata) {
  metadata_ = std::move(metadata);
  return *this;
}

LineBuilder& LineBuilder::DeleteCharacters(ColumnNumber column,
                                           ColumnNumberDelta delta) {
  ValidateInvariants();
  CHECK_GE(delta, ColumnNumberDelta(0));
  CHECK_LE(column, EndColumn());
  CHECK_LE(column + delta, EndColumn());

  contents = lazy_string::Append(
      lazy_string::Substring(contents, ColumnNumber(0), column.ToDelta()),
      lazy_string::Substring(contents, column + delta));

  std::map<ColumnNumber, LineModifierSet> new_modifiers;
  // TODO: We could optimize this to only set it once (rather than for every
  // modifier before the deleted range).
  std::optional<LineModifierSet> last_modifiers_before_gap;
  std::optional<LineModifierSet> modifiers_continuation;
  for (auto& m : modifiers) {
    if (m.first < column) {
      last_modifiers_before_gap = m.second;
      new_modifiers[m.first] = std::move(m.second);
    } else if (m.first < column + delta) {
      modifiers_continuation = std::move(m.second);
    } else {
      new_modifiers[m.first - delta] = std::move(m.second);
    }
  }
  if (modifiers_continuation.has_value() &&
      new_modifiers.find(column) == new_modifiers.end() &&
      last_modifiers_before_gap != modifiers_continuation &&
      column + delta < EndColumn()) {
    new_modifiers[column] = modifiers_continuation.value();
  }
  modifiers = std::move(new_modifiers);
  metadata_ = std::nullopt;

  ValidateInvariants();
  return *this;
}

LineBuilder& LineBuilder::DeleteSuffix(ColumnNumber column) {
  if (column >= EndColumn()) return *this;
  return DeleteCharacters(column, EndColumn() - column);
}

LineBuilder& LineBuilder::SetAllModifiers(LineModifierSet value) {
  modifiers = {{ColumnNumber(0), value}};
  end_of_line_modifiers_ = std::move(value);
  return *this;
}

LineBuilder& LineBuilder::insert_end_of_line_modifiers(LineModifierSet values) {
  end_of_line_modifiers_.insert(values.begin(), values.end());
  return *this;
}

LineModifierSet LineBuilder::copy_end_of_line_modifiers() const {
  return end_of_line_modifiers_;
}

void LineBuilder::ValidateInvariants() {}

/* static */ NonNull<std::shared_ptr<Line>> Line::New(LineBuilder options) {
  return MakeNonNullShared<Line>(std::move(options));
}

Line::Line(std::wstring x) : Line(LineBuilder(NewLazyString(std::move(x)))) {}

Line::Line(LineBuilder options)
    : data_(Data{.options = std::move(options)}, Line::ValidateInvariants) {}

Line::Line(const Line& line)
    : data_(line.data_.lock([](const Data& line_data) {
        return Data{.options = line_data.options, .hash = line_data.hash};
      }),
            Line::ValidateInvariants) {}

LineBuilder Line::CopyLineBuilder() const {
  return data_.lock([](const Data& data) { return data.options; });
}

LineBuilder Line::GetLineBuilder() && {
  return data_.lock([](Data& data) { return std::move(data.options); });
}

NonNull<std::shared_ptr<LazyString>> Line::contents() const {
  return data_.lock([](const Data& data) { return data.options.contents; });
}

ColumnNumber Line::EndColumn() const {
  return data_.lock([](const Data& data) { return EndColumn(data); });
}

bool Line::empty() const { return EndColumn().IsZero(); }

wint_t Line::get(ColumnNumber column) const {
  return data_.lock([column](const Data& data) { return Get(data, column); });
}

NonNull<std::shared_ptr<LazyString>> Line::Substring(
    ColumnNumber column, ColumnNumberDelta delta) const {
  return lazy_string::Substring(contents(), column, delta);
}

NonNull<std::shared_ptr<LazyString>> Line::Substring(
    ColumnNumber column) const {
  return lazy_string::Substring(contents(), column);
}

std::shared_ptr<LazyString> Line::metadata() const {
  return data_.lock([](const Data& data) -> std::shared_ptr<LazyString> {
    if (const auto& metadata = data.options.metadata_; metadata.has_value())
      return metadata->value.get_copy()
          .value_or(metadata->initial_value)
          .get_shared();
    return nullptr;
  });
}

language::ValueOrError<futures::ListenableValue<
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>>>
Line::metadata_future() const {
  return data_.lock(
      [](const Data& data)
          -> language::ValueOrError<
              futures::ListenableValue<NonNull<std::shared_ptr<LazyString>>>> {
        if (const auto& metadata = data.options.metadata_;
            metadata.has_value()) {
          return metadata.value().value;
        }
        return Error(L"Line has no value.");
      });
}

std::function<void()> Line::explicit_delete_observer() const {
  return data_.lock(
      [](const Data& data) { return data.options.explicit_delete_observer_; });
}

std::optional<BufferLineColumn> Line::buffer_line_column() const {
  return data_.lock(
      [](const Data& data) { return data.options.buffer_line_column_; });
}

LineWithCursor Line::Output(const OutputOptions& options) const {
  static Tracker tracker(L"Line::Output");
  auto tracker_call = tracker.Call();

  VLOG(5) << "Producing output of line: " << ToString();
  return data_.lock([&options](const Data& data) {
    LineBuilder line_output;
    ColumnNumber input_column = options.initial_column;
    LineWithCursor line_with_cursor;
    auto modifiers_it = data.options.modifiers.lower_bound(input_column);
    if (!data.options.modifiers.empty() &&
        modifiers_it != data.options.modifiers.begin()) {
      line_output.modifiers[ColumnNumber()] = std::prev(modifiers_it)->second;
    }

    const ColumnNumber input_end =
        options.input_width != std::numeric_limits<ColumnNumberDelta>::max()
            ? std::min(EndColumn(data), input_column + options.input_width)
            : EndColumn(data);
    // output_column contains the column in the screen. May not match
    // options.contents.size() if there are wide characters.
    for (ColumnNumber output_column;
         input_column <= input_end && output_column.ToDelta() < options.width;
         ++input_column) {
      wint_t c = input_column < input_end ? Get(data, input_column) : L' ';
      CHECK(c != '\n');

      ColumnNumber current_position =
          ColumnNumber() + line_output.contents->size();
      if (modifiers_it != data.options.modifiers.end()) {
        CHECK_GE(modifiers_it->first, input_column);
        if (modifiers_it->first == input_column) {
          line_output.modifiers[current_position] = modifiers_it->second;
          ++modifiers_it;
        }
      }

      if (options.active_cursor_column.has_value() &&
          (options.active_cursor_column.value() == input_column ||
           (input_column == input_end &&
            options.active_cursor_column.value() >= input_column))) {
        // We use current_position rather than output_column because terminals
        // compensate for wide characters (so we don't need to).
        line_with_cursor.cursor = current_position;
        if (!options.modifiers_main_cursor.empty()) {
          line_output.modifiers[current_position + ColumnNumberDelta(1)] =
              line_output.modifiers.empty()
                  ? LineModifierSet()
                  : line_output.modifiers.rbegin()->second;
          line_output.modifiers[current_position].insert(
              options.modifiers_main_cursor.begin(),
              options.modifiers_main_cursor.end());
        }
      } else if (options.inactive_cursor_columns.find(input_column) !=
                     options.inactive_cursor_columns.end() ||
                 (input_column == input_end &&
                  !options.inactive_cursor_columns.empty() &&
                  *options.inactive_cursor_columns.rbegin() >= input_column)) {
        line_output.modifiers[current_position + ColumnNumberDelta(1)] =
            line_output.modifiers.empty()
                ? LineModifierSet()
                : line_output.modifiers.rbegin()->second;
        line_output.modifiers[current_position].insert(
            options.modifiers_inactive_cursors.begin(),
            options.modifiers_inactive_cursors.end());
      }

      switch (c) {
        case L'\r':
          break;

        case L'\t': {
          ColumnNumber target =
              ColumnNumber(0) +
              ((output_column.ToDelta() / 8) + ColumnNumberDelta(1)) * 8;
          VLOG(8) << "Handling TAB character at position: " << output_column
                  << ", target: " << target;
          line_output.AppendString(Padding(target - output_column, L' '),
                                   std::nullopt);
          output_column = target;
          break;
        }

        default:
          VLOG(8) << "Print character: " << c;
          output_column += ColumnNumberDelta(wcwidth(c));
          if (output_column.ToDelta() <= options.width)
            line_output.contents =
                lazy_string::Append(std::move(line_output.contents),
                                    NewLazyString(std::wstring(1, c)));
      }
    }

    line_output.end_of_line_modifiers_ =
        input_column == EndColumn(data)
            ? data.options.end_of_line_modifiers_
            : (line_output.modifiers.empty()
                   ? LineModifierSet()
                   : line_output.modifiers.rbegin()->second);
    if (!line_with_cursor.cursor.has_value() &&
        options.active_cursor_column.has_value()) {
      // Same as above: we use the current position (rather than output_column)
      // since terminals compensate for wide characters.
      line_with_cursor.cursor = ColumnNumber() + line_output.contents->size();
    }

    line_with_cursor.line = MakeNonNullShared<Line>(std::move(line_output));
    return line_with_cursor;
  });
}

/* static */ void Line::ValidateInvariants(const Data& data) {
  static Tracker tracker(L"Line::ValidateInvariants");
  auto call = tracker.Call();
#if 0
  ForEachColumn(Pointer(data.options.contents).Reference(),
                [&contents = data.options.contents](ColumnNumber, wchar_t c) {
                  CHECK(c != L'\n')
                      << "Line has newline character: " << contents->ToString();
                });
#endif
  for (auto& m : data.options.modifiers) {
    CHECK_LE(m.first, EndColumn(data)) << "Modifiers found past end of line.";
    CHECK(m.second.find(LineModifier::kReset) == m.second.end());
  }
}

/* static */ ColumnNumber Line::EndColumn(const Data& data) {
  return ColumnNumber(0) + data.options.contents->size();
}

wint_t Line::Get(const Data& data, ColumnNumber column) {
  CHECK_LT(column, EndColumn(data));
  return data.options.contents->get(column);
}

}  // namespace editor
}  // namespace afc
namespace std {
std::size_t hash<afc::editor::Line>::operator()(
    const afc::editor::Line& line) const {
  using namespace afc::editor;
  return line.data_.lock([](const Line::Data& data) {
    if (data.hash.has_value()) return *data.hash;
    data.hash = compute_hash(
        data.options.contents.value(),
        MakeHashableIteratorRange(data.options.end_of_line_modifiers_),
        MakeHashableIteratorRange(
            data.options.modifiers.begin(), data.options.modifiers.end(),
            [](const std::pair<ColumnNumber, LineModifierSet>& value) {
              return compute_hash(value.first,
                                  MakeHashableIteratorRange(value.second));
            }));
    return *data.hash;
  });
}
}  // namespace std
