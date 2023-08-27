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
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
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
            Line line = std::move(options).Build();
            CHECK(line.end_of_line_modifiers().find(LineModifier::kRed) !=
                  line.end_of_line_modifiers().end());
          }},
     {.name = L"CopyOfEmptyPreservesEndOfLine",
      .callback =
          [] {
            LineBuilder options;
            options.insert_end_of_line_modifiers({LineModifier::kRed});
            Line initial_line = std::move(options).Build();
            Line final_line(std::move(initial_line));
            CHECK(final_line.end_of_line_modifiers().find(LineModifier::kRed) !=
                  final_line.end_of_line_modifiers().end());
          }},
     {.name = L"EndOfLineModifiersChangesHash",
      .callback =
          [] {
            LineBuilder options;
            size_t initial_hash = std::hash<Line>{}(options.Copy().Build());
            options.insert_end_of_line_modifiers({LineModifier::kRed});
            size_t final_hash = std::hash<Line>{}(std::move(options).Build());
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"ContentChangesHash",
      .callback =
          [] {
            CHECK(std::hash<Line>{}(
                      LineBuilder(NewLazyString(L"alejo")).Build()) !=
                  std::hash<Line>{}(
                      LineBuilder(NewLazyString(L"Xlejo")).Build()));
          }},
     {.name = L"ModifiersChangesHash",
      .callback =
          [] {
            LineBuilder options(NewLazyString(L"alejo"));
            size_t initial_hash = std::hash<Line>{}(options.Copy().Build());
            options.InsertModifier(ColumnNumber(2), LineModifier::kRed);
            size_t final_hash = std::hash<Line>{}(std::move(options).Build());
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"MetadataBecomesAvailable", .callback = [] {
        futures::Future<NonNull<std::shared_ptr<LazyString>>> future;
        LineBuilder builder;
        builder.SetMetadata(
            LineMetadataEntry{.initial_value = NewLazyString(L"Foo"),
                              .value = std::move(future.value)});
        Line line = std::move(builder).Build();
        CHECK(line.metadata()->ToString() == L"Foo");
        future.consumer(NewLazyString(L"Bar"));
        CHECK(line.metadata()->ToString() == L"Bar");
      }}});
}

LineBuilder::LineBuilder(Line&& line) : data_(std::move(line.stable_fields_)) {}

LineBuilder::LineBuilder(const Line& line) : data_(line.stable_fields_) {}

LineBuilder::LineBuilder(
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
        input_contents)
    : data_(Line::Data{.contents = std::move(input_contents)}) {}

LineBuilder::LineBuilder(Line::Data data) : data_(std::move(data)) {}

LineBuilder LineBuilder::Copy() const { return LineBuilder(data_); }

Line LineBuilder::Build() && { return Line(std::move(data_)); }

ColumnNumber LineBuilder::EndColumn() const {
  // TODO: Compute this separately, taking the width of characters into account.
  return ColumnNumber(0) + contents()->size();
}

void LineBuilder::SetCharacter(ColumnNumber column, int c,
                               const LineModifierSet& c_modifiers) {
  ValidateInvariants();
  VLOG(4) << "Start SetCharacter: " << column;
  auto str = NewLazyString(std::wstring(1, c));
  if (column >= EndColumn()) {
    column = EndColumn();
    data_.contents =
        lazy_string::Append(std::move(data_.contents), std::move(str));
  } else {
    data_.contents = lazy_string::Append(
        lazy_string::Substring(std::move(data_.contents), ColumnNumber(0),
                               column.ToDelta()),
        std::move(str),
        lazy_string::Substring(data_.contents, column + ColumnNumberDelta(1)));
  }

  data_.metadata = std::nullopt;

  // Return the modifiers that are effective at a given position.
  auto Read = [&](ColumnNumber position) -> LineModifierSet {
    if (data_.modifiers.empty() || data_.modifiers.begin()->first > position)
      return {};
    auto it = data_.modifiers.lower_bound(position);
    if (it == data_.modifiers.end() || it->first > position) --it;
    return it->second;
  };

  auto Set = [&](ColumnNumber position, LineModifierSet value) {
    LineModifierSet previous_value =
        position.IsZero() ? LineModifierSet{}
                          : Read(position - ColumnNumberDelta(1));
    if (previous_value == value)
      data_.modifiers.erase(position);
    else
      data_.modifiers[position] = value;
  };

  ColumnNumber after_column = column + ColumnNumberDelta(1);
  LineModifierSet modifiers_after_column = Read(after_column);

  Set(column, c_modifiers);

  if (after_column < EndColumn()) Set(after_column, modifiers_after_column);

  ValidateInvariants();

  for (std::pair<ColumnNumber, LineModifierSet> entry : data_.modifiers)
    VLOG(5) << "Modifiers: " << entry.first << ": " << entry.second;
}

namespace {
const bool line_set_character_tests_registration = tests::Register(
    L"LineTestsSetCharacter",
    {{.name = L"ConsecutiveSets", .callback = [] {
        LineBuilder options;
        options.AppendString(std::wstring(L"ALEJANDRO"), std::nullopt);
        CHECK(options.contents()->ToString() == L"ALEJANDRO");
        CHECK(options.modifiers().empty());

        options.SetCharacter(ColumnNumber(1), L'l',
                             LineModifierSet{LineModifier::kBold});
        CHECK(options.contents()->ToString() == L"AlEJANDRO");
        CHECK_EQ(options.modifiers().size(), 2ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(2))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(2), L'e',
                             LineModifierSet{LineModifier::kBold});
        CHECK(options.contents()->ToString() == L"AleJANDRO");
        CHECK_EQ(options.modifiers().size(), 2ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(3), L'j',
                             LineModifierSet{LineModifier::kUnderline});
        CHECK(options.contents()->ToString() == L"AlejANDRO");
        CHECK_EQ(options.modifiers().size(), 3ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(5), L'n',
                             LineModifierSet{LineModifier::kBlue});
        CHECK(options.contents()->ToString() == L"AlejAnDRO");
        CHECK_EQ(options.modifiers().size(), 5ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second,
                 LineModifierSet{});
        CHECK_EQ(options.modifiers().find(ColumnNumber(5))->second,
                 LineModifierSet{LineModifier::kBlue});
        CHECK_EQ(options.modifiers().find(ColumnNumber(6))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(4), L'a',
                             LineModifierSet{LineModifier::kRed});
        CHECK(options.contents()->ToString() == L"AlejanDRO");
        CHECK_EQ(options.modifiers().size(), 5ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second,
                 LineModifierSet{LineModifier::kRed});
        CHECK_EQ(options.modifiers().find(ColumnNumber(5))->second,
                 LineModifierSet{LineModifier::kBlue});
        CHECK_EQ(options.modifiers().find(ColumnNumber(6))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(0), L'a',
                             LineModifierSet{LineModifier::kBold});
        CHECK(options.contents()->ToString() == L"alejanDRO");
        CHECK_EQ(options.modifiers().size(), 5ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(0))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second,
                 LineModifierSet{LineModifier::kRed});
        CHECK_EQ(options.modifiers().find(ColumnNumber(5))->second,
                 LineModifierSet{LineModifier::kBlue});
        CHECK_EQ(options.modifiers().find(ColumnNumber(6))->second,
                 LineModifierSet{});
      }}});
}  // namespace

void LineBuilder::InsertCharacterAtPosition(ColumnNumber column) {
  ValidateInvariants();
  set_contents(lazy_string::Append(
      lazy_string::Substring(data_.contents, ColumnNumber(0), column.ToDelta()),
      NewLazyString(L" "), lazy_string::Substring(data_.contents, column)));

  std::map<ColumnNumber, LineModifierSet> new_modifiers;
  for (auto& m : data_.modifiers) {
    new_modifiers[m.first + (m.first < column ? ColumnNumberDelta(0)
                                              : ColumnNumberDelta(1))] =
        std::move(m.second);
  }
  set_modifiers(std::move(new_modifiers));
  SetMetadata(std::nullopt);
  ValidateInvariants();
}

void LineBuilder::AppendCharacter(wchar_t c, LineModifierSet modifier) {
  ValidateInvariants();
  CHECK(modifier.find(LineModifier::kReset) == modifier.end());
  data_.modifiers[ColumnNumber(0) + data_.contents->size()] = modifier;
  data_.contents = lazy_string::Append(std::move(data_.contents),
                                       NewLazyString(std::wstring(1, c)));
  SetMetadata(std::nullopt);
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
      suffix_line.data_.contents->size() > ColumnNumberDelta(0)) {
    suffix_line.data_.modifiers[ColumnNumber(0)] = suffix_modifiers.value();
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
  data_.end_of_line_modifiers = std::move(line.data_.end_of_line_modifiers);
  if (line.EndColumn().IsZero()) return;
  ColumnNumberDelta original_length = EndColumn().ToDelta();
  data_.contents = lazy_string::Append(std::move(data_.contents),
                                       std::move(line.data_.contents));
  SetMetadata(std::nullopt);

  auto initial_modifier =
      line.data_.modifiers.empty() ||
              line.data_.modifiers.begin()->first != ColumnNumber(0)
          ? LineModifierSet{}
          : line.data_.modifiers.begin()->second;
  auto final_modifier = data_.modifiers.empty()
                            ? LineModifierSet{}
                            : data_.modifiers.rbegin()->second;
  if (initial_modifier != final_modifier) {
    data_.modifiers[ColumnNumber() + original_length] = initial_modifier;
  }
  for (auto& [position, new_modifiers] : line.data_.modifiers) {
    if ((data_.modifiers.empty()
             ? LineModifierSet{}
             : data_.modifiers.rbegin()->second) != new_modifiers) {
      data_.modifiers[position + original_length] = std::move(new_modifiers);
    }
  }

  data_.end_of_line_modifiers = line.data_.end_of_line_modifiers;

  ValidateInvariants();
}

void LineBuilder::SetBufferLineColumn(BufferLineColumn buffer_line_column) {
  data_.buffer_line_column = buffer_line_column;
}
std::optional<BufferLineColumn> LineBuilder::buffer_line_column() const {
  return data_.buffer_line_column;
}

LineBuilder& LineBuilder::SetMetadata(
    std::optional<LineMetadataEntry> metadata) {
  data_.metadata = std::move(metadata);
  return *this;
}

LineBuilder& LineBuilder::DeleteCharacters(ColumnNumber column,
                                           ColumnNumberDelta delta) {
  ValidateInvariants();
  CHECK_GE(delta, ColumnNumberDelta(0));
  CHECK_LE(column, EndColumn());
  CHECK_LE(column + delta, EndColumn());

  data_.contents = lazy_string::Append(
      lazy_string::Substring(data_.contents, ColumnNumber(0), column.ToDelta()),
      lazy_string::Substring(data_.contents, column + delta));

  std::map<ColumnNumber, LineModifierSet> new_modifiers;
  // TODO: We could optimize this to only set it once (rather than for every
  // modifier before the deleted range).
  std::optional<LineModifierSet> last_modifiers_before_gap;
  std::optional<LineModifierSet> modifiers_continuation;
  for (auto& m : data_.modifiers) {
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
  set_modifiers(std::move(new_modifiers));
  data_.metadata = std::nullopt;

  ValidateInvariants();
  return *this;
}

LineBuilder& LineBuilder::DeleteSuffix(ColumnNumber column) {
  if (column >= EndColumn()) return *this;
  return DeleteCharacters(column, EndColumn() - column);
}

LineBuilder& LineBuilder::SetAllModifiers(LineModifierSet value) {
  set_modifiers({{ColumnNumber(0), value}});
  data_.end_of_line_modifiers = std::move(value);
  return *this;
}

LineBuilder& LineBuilder::insert_end_of_line_modifiers(LineModifierSet values) {
  data_.end_of_line_modifiers.insert(values.begin(), values.end());
  return *this;
}

LineModifierSet LineBuilder::copy_end_of_line_modifiers() const {
  return data_.end_of_line_modifiers;
}

std::map<language::lazy_string::ColumnNumber, LineModifierSet>
LineBuilder::modifiers() const {
  return data_.modifiers;
}

size_t LineBuilder::modifiers_size() const { return data_.modifiers.size(); }

void LineBuilder::InsertModifier(language::lazy_string::ColumnNumber position,
                                 LineModifier modifier) {
  data_.modifiers[position].insert(modifier);
}

void LineBuilder::set_modifiers(language::lazy_string::ColumnNumber position,
                                LineModifierSet value) {
  data_.modifiers[position] = std::move(value);
}

void LineBuilder::set_modifiers(
    std::map<language::lazy_string::ColumnNumber, LineModifierSet> value) {
  data_.modifiers = std::move(value);
}

void LineBuilder::ClearModifiers() { data_.modifiers.clear(); }

language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
LineBuilder::contents() const {
  return data_.contents;
}

void LineBuilder::set_contents(
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
        value) {
  data_.contents = std::move(value);
}

void LineBuilder::ValidateInvariants() {}

Line::Line(std::wstring x)
    : Line(Data{.contents = NewLazyString(std::move(x))}) {}

Line::Line(const Line& line)
    : stable_fields_(line.stable_fields_), hash_(ComputeHash(stable_fields_)) {}

/* static */
size_t Line::ComputeHash(const Line::Data& data) {
  return compute_hash(
      data.contents.value(),
      MakeHashableIteratorRange(
          data.modifiers.begin(), data.modifiers.end(),
          [](const std::pair<ColumnNumber, LineModifierSet>& value) {
            return compute_hash(value.first,
                                MakeHashableIteratorRange(value.second));
          }),
      MakeHashableIteratorRange(data.end_of_line_modifiers), data.metadata);
}

NonNull<std::shared_ptr<LazyString>> Line::contents() const {
  return stable_fields_.contents;
}

ColumnNumber Line::EndColumn() const {
  return ColumnNumber(0) + stable_fields_.contents->size();
}

bool Line::empty() const { return EndColumn().IsZero(); }

wint_t Line::get(ColumnNumber column) const { return Get(column); }

NonNull<std::shared_ptr<LazyString>> Line::Substring(
    ColumnNumber column, ColumnNumberDelta delta) const {
  return lazy_string::Substring(contents(), column, delta);
}

NonNull<std::shared_ptr<LazyString>> Line::Substring(
    ColumnNumber column) const {
  return lazy_string::Substring(contents(), column);
}

std::shared_ptr<LazyString> Line::metadata() const {
  if (const auto& metadata = stable_fields_.metadata; metadata.has_value())
    return metadata->value.get_copy()
        .value_or(metadata->initial_value)
        .get_shared();
  return nullptr;
}

language::ValueOrError<futures::ListenableValue<
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>>>
Line::metadata_future() const {
  if (const auto& metadata = stable_fields_.metadata; metadata.has_value()) {
    return metadata.value().value;
  }
  return Error(L"Line has no value.");
}

std::function<void()> Line::explicit_delete_observer() const {
  return stable_fields_.explicit_delete_observer;
}

std::optional<BufferLineColumn> Line::buffer_line_column() const {
  return stable_fields_.buffer_line_column;
}

Line::Line(Line::Data stable_fields)
    : stable_fields_(std::move(stable_fields)),
      hash_(ComputeHash(stable_fields_)) {
  for (auto& m : stable_fields_.modifiers) {
    CHECK_LE(m.first, EndColumn()) << "Modifiers found past end of line.";
    CHECK(m.second.find(LineModifier::kReset) == m.second.end());
  }
#if 0
  static Tracker tracker(L"Line::ValidateInvariants");
  auto call = tracker.Call();
  ForEachColumn(Pointer(stable_fields_.contents).Reference(),
                [&contents = stable_fields_.contents](ColumnNumber, wchar_t c) {
                  CHECK(c != L'\n')
                      << "Line has newline character: " << contents->ToString();
                });
#endif
}

wint_t Line::Get(ColumnNumber column) const {
  CHECK_LT(column, EndColumn());
  return stable_fields_.contents->get(column);
}

}  // namespace editor
}  // namespace afc
