#include "src/line.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/padding.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc {
namespace editor {
namespace lazy_string = language::lazy_string;

using infrastructure::Tracker;
using language::compute_hash;
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
            Line::Options options;
            options.end_of_line_modifiers.insert(LineModifier::RED);
            Line line(std::move(options));
            CHECK(line.end_of_line_modifiers().find(LineModifier::RED) !=
                  line.end_of_line_modifiers().end());
          }},
     {.name = L"CopyOfEmptyPreservesEndOfLine",
      .callback =
          [] {
            Line::Options options;
            options.end_of_line_modifiers.insert(LineModifier::RED);
            Line initial_line(std::move(options));
            Line final_line(std::move(initial_line));
            CHECK(final_line.end_of_line_modifiers().find(LineModifier::RED) !=
                  final_line.end_of_line_modifiers().end());
          }},
     {.name = L"EndOfLineModifiersChangesHash",
      .callback =
          [] {
            Line::Options options;
            size_t initial_hash = std::hash<Line>{}(Line(options));
            options.end_of_line_modifiers.insert(LineModifier::RED);
            size_t final_hash = std::hash<Line>{}(Line(options));
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"ContentChangesHash",
      .callback =
          [] {
            CHECK(std::hash<Line>{}(
                      Line(Line::Options(NewLazyString(L"alejo")))) !=
                  std::hash<Line>{}(
                      Line(Line::Options(NewLazyString(L"Xlejo")))));
          }},
     {.name = L"ModifiersChangesHash",
      .callback =
          [] {
            Line::Options options(NewLazyString(L"alejo"));
            size_t initial_hash = std::hash<Line>{}(Line(options));
            options.modifiers[ColumnNumber(2)].insert(LineModifier::RED);
            size_t final_hash = std::hash<Line>{}(Line(options));
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"MetadataBecomesAvailable", .callback = [] {
        futures::Future<NonNull<std::shared_ptr<LazyString>>> future;
        Line line(Line::Options().SetMetadata(
            Line::MetadataEntry{.initial_value = NewLazyString(L"Foo"),
                                .value = std::move(future.value)}));
        CHECK(line.metadata()->ToString() == L"Foo");
        future.consumer(NewLazyString(L"Bar"));
        CHECK(line.metadata()->ToString() == L"Bar");
      }}});
}

ColumnNumber Line::Options::EndColumn() const {
  // TODO: Compute this separately, taking the width of characters into account.
  return ColumnNumber(0) + contents->size();
}

void Line::Options::SetCharacter(ColumnNumber column, int c,
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

  metadata = std::nullopt;

  // Modifiers that would be used at column (before this call to SetCharacter).
  // We may need to make sure they get applied at column + 1.
  LineModifierSet previous_modifiers;
  if (!modifiers.empty() && modifiers.begin()->first <= column) {
    // Smallest entry greater than or equal to column.
    auto it = modifiers.lower_bound(column);
    if (it == modifiers.end() || it->first > column) --it;
    previous_modifiers = it->second;
  }
  if (c_modifiers != previous_modifiers) {
    modifiers[column] = c_modifiers;
    if (column + ColumnNumberDelta(1) < EndColumn()) {
      // We only insert if no value was already present.
      modifiers.insert({column + ColumnNumberDelta(1), previous_modifiers});
    }
    ValidateInvariants();
  }

  for (std::pair<ColumnNumber, LineModifierSet> entry : modifiers)
    VLOG(5) << "Modifiers: " << entry.first << ": " << entry.second;
}

namespace {
const bool line_set_character_tests_registration = tests::Register(
    L"LineTestsSetCharacter",
    {{.name = L"ConsecutiveSets", .callback = [] {
        Line::Options options;
        options.AppendString(std::wstring(L"ALEJANDRO"), std::nullopt);
        CHECK(options.contents->ToString() == L"ALEJANDRO");
        CHECK(options.modifiers.empty());

        options.SetCharacter(ColumnNumber(0), L'a',
                             LineModifierSet{LineModifier::BOLD});
        CHECK(options.contents->ToString() == L"aLEJANDRO");
        CHECK_EQ(options.modifiers.size(), 2ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(0))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(1), L'l',
                             LineModifierSet{LineModifier::BOLD});
        CHECK(options.contents->ToString() == L"alEJANDRO");
        CHECK_EQ(options.modifiers.size(), 3ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(0))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(2))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(2), L'e',
                             LineModifierSet{LineModifier::UNDERLINE});
        CHECK(options.contents->ToString() == L"aleJANDRO");
        CHECK_EQ(options.modifiers.size(), 4ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(0))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(2))->second,
                 LineModifierSet{LineModifier::UNDERLINE});
        CHECK_EQ(options.modifiers.find(ColumnNumber(3))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(4), L'a',
                             LineModifierSet{LineModifier::BLUE});
        CHECK(options.contents->ToString() == L"aleJaNDRO");
        CHECK_EQ(options.modifiers.size(), 6ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(0))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(2))->second,
                 LineModifierSet{LineModifier::UNDERLINE});
        CHECK_EQ(options.modifiers.find(ColumnNumber(3))->second,
                 LineModifierSet{});
        CHECK_EQ(options.modifiers.find(ColumnNumber(4))->second,
                 LineModifierSet{LineModifier::BLUE});
        CHECK_EQ(options.modifiers.find(ColumnNumber(5))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(3), L'j',
                             LineModifierSet{LineModifier::RED});
        CHECK(options.contents->ToString() == L"alejaNDRO");
        CHECK_EQ(options.modifiers.size(), 6ul);
        CHECK_EQ(options.modifiers.find(ColumnNumber(0))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::BOLD});
        CHECK_EQ(options.modifiers.find(ColumnNumber(2))->second,
                 LineModifierSet{LineModifier::UNDERLINE});
        CHECK_EQ(options.modifiers.find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::RED});
        CHECK_EQ(options.modifiers.find(ColumnNumber(4))->second,
                 LineModifierSet{LineModifier::BLUE});
        CHECK_EQ(options.modifiers.find(ColumnNumber(5))->second,
                 LineModifierSet{});
      }}});
}  // namespace

void Line::Options::InsertCharacterAtPosition(ColumnNumber column) {
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

  metadata = std::nullopt;
  ValidateInvariants();
}

void Line::Options::AppendCharacter(wchar_t c, LineModifierSet modifier) {
  ValidateInvariants();
  CHECK(modifier.find(LineModifier::RESET) == modifier.end());
  modifiers[ColumnNumber(0) + contents->size()] = modifier;
  contents = lazy_string::Append(std::move(contents),
                                 NewLazyString(std::wstring(1, c)));
  metadata = std::nullopt;
  ValidateInvariants();
}

void Line::Options::AppendString(NonNull<std::shared_ptr<LazyString>> suffix) {
  AppendString(std::move(suffix), std::nullopt);
}

void Line::Options::AppendString(
    NonNull<std::shared_ptr<LazyString>> suffix,
    std::optional<LineModifierSet> suffix_modifiers) {
  ValidateInvariants();
  Line::Options suffix_line(std::move(suffix));
  if (suffix_modifiers.has_value() &&
      suffix_line.contents->size() > ColumnNumberDelta(0)) {
    suffix_line.modifiers[ColumnNumber(0)] = suffix_modifiers.value();
  }
  Append(Line(std::move(suffix_line)));
  ValidateInvariants();
}

void Line::Options::AppendString(std::wstring suffix,
                                 std::optional<LineModifierSet> modifiers) {
  AppendString(NewLazyString(std::move(suffix)), std::move(modifiers));
}

void Line::Options::Append(Line line) {
  ValidateInvariants();
  end_of_line_modifiers = std::move(line.end_of_line_modifiers());
  if (line.empty()) return;
  auto original_length = EndColumn().ToDelta();
  contents =
      lazy_string::Append(std::move(contents), std::move(line.contents()));
  metadata = std::nullopt;

  auto initial_modifier =
      line.modifiers().empty() ||
              line.modifiers().begin()->first != ColumnNumber(0)
          ? LineModifierSet{}
          : line.modifiers().begin()->second;
  auto final_modifier =
      modifiers.empty() ? LineModifierSet{} : modifiers.rbegin()->second;
  if (initial_modifier != final_modifier) {
    modifiers[ColumnNumber() + original_length] = initial_modifier;
  }
  for (auto& [position, new_modifiers] : line.modifiers()) {
    if ((modifiers.empty() ? LineModifierSet{} : modifiers.rbegin()->second) !=
        new_modifiers) {
      modifiers[position + original_length] = std::move(new_modifiers);
    }
  }

  ValidateInvariants();
}

void Line::Options::SetBufferLineColumn(
    Line::BufferLineColumn buffer_line_column) {
  buffer_line_column_ = buffer_line_column;
}
std::optional<Line::BufferLineColumn> Line::Options::buffer_line_column()
    const {
  return buffer_line_column_;
}

Line::Options& Line::Options::SetMetadata(
    std::optional<MetadataEntry> metadata) {
  this->metadata = std::move(metadata);
  return *this;
}

Line::Options& Line::Options::DeleteCharacters(ColumnNumber column,
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
  metadata = std::nullopt;

  ValidateInvariants();
  return *this;
}

Line::Options& Line::Options::DeleteSuffix(ColumnNumber column) {
  return DeleteCharacters(column, EndColumn() - column);
}

void Line::Options::ValidateInvariants() {}

/* static */ NonNull<std::shared_ptr<Line>> Line::New(Options options) {
  return MakeNonNullShared<Line>(std::move(options));
}

Line::Line(std::wstring x) : Line(Line::Options(NewLazyString(std::move(x)))) {}

Line::Line(Options options)
    : data_(Data{.options = std::move(options)}, Line::ValidateInvariants) {}

Line::Line(const Line& line) : data_(Data{}, Line::ValidateInvariants) {
  data_.lock([&line](Data& data) {
    line.data_.lock([&data](const Data& line_data) {
      data.options = line_data.options;
      data.hash = line_data.hash;
    });
  });
}

Line::Options Line::CopyOptions() const {
  return data_.lock([](const Data& data) { return data.options; });
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
    if (const auto& metadata = data.options.metadata; metadata.has_value())
      return metadata->value.get_copy()
          .value_or(metadata->initial_value)
          .get_shared();
    return nullptr;
  });
}

void Line::SetAllModifiers(const LineModifierSet& modifiers) {
  data_.lock([&modifiers](Data& data) {
    data.options.modifiers.clear();
    data.options.modifiers[ColumnNumber(0)] = modifiers;
    data.options.end_of_line_modifiers = modifiers;
    data.hash = std::nullopt;
  });
}

void Line::Append(const Line& line) {
  CHECK(this != &line);
  if (line.empty()) return;
  data_.lock([&line](Data& data) {
    data.hash = std::nullopt;
    line.data_.lock([&data](const Data& line_data) {
      auto original_length = EndColumn(data).ToDelta();
      data.options.contents = lazy_string::Append(
          std::move(data.options.contents), line_data.options.contents);
      data.options.modifiers[ColumnNumber() + original_length] =
          LineModifierSet{};
      for (auto& [position, modifiers] : line_data.options.modifiers) {
        data.options.modifiers[position + original_length] = modifiers;
      }
      data.options.end_of_line_modifiers =
          line_data.options.end_of_line_modifiers;
      data.options.metadata = std::nullopt;
    });
  });
}

std::function<void()> Line::explicit_delete_observer() const {
  return data_.lock(
      [](const Data& data) { return data.options.explicit_delete_observer_; });
}

std::optional<Line::BufferLineColumn> Line::buffer_line_column() const {
  return data_.lock(
      [](const Data& data) { return data.options.buffer_line_column_; });
}

LineWithCursor Line::Output(const OutputOptions& options) const {
  static Tracker tracker(L"Line::Output");
  auto tracker_call = tracker.Call();

  VLOG(5) << "Producing output of line: " << ToString();
  return data_.lock([&options](const Data& data) {
    Line::Options line_output;
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

    line_output.end_of_line_modifiers =
        input_column == EndColumn(data)
            ? data.options.end_of_line_modifiers
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
    CHECK(m.second.find(LineModifier::RESET) == m.second.end());
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
        MakeHashableIteratorRange(data.options.end_of_line_modifiers),
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
