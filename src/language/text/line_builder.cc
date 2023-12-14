#include "src/language/text/line_builder.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;

namespace afc::language::text {

namespace {
const bool line_tests_registration = tests::Register(
    L"LineTests",
    {{.name = L"ConstructionOfEmptySetsEndOfLine",
      .callback =
          [] {
            LineBuilder options;
            options.insert_end_of_line_modifiers({LineModifier::kRed});
            CHECK(std::move(options).Build()->end_of_line_modifiers().contains(
                LineModifier::kRed));
          }},
     {.name = L"CopyOfEmptyPreservesEndOfLine",
      .callback =
          [] {
            LineBuilder options;
            options.insert_end_of_line_modifiers({LineModifier::kRed});
            NonNull<std::shared_ptr<Line>> initial_line =
                std::move(options).Build();
            Line final_line(std::move(initial_line.value()));
            CHECK(final_line.end_of_line_modifiers().contains(
                LineModifier::kRed));
          }},
     {.name = L"EndOfLineModifiersChangesHash",
      .callback =
          [] {
            LineBuilder options;
            size_t initial_hash =
                std::hash<Line>{}(options.Copy().Build().value());
            options.insert_end_of_line_modifiers({LineModifier::kRed});
            size_t final_hash =
                std::hash<Line>{}(std::move(options).Build().value());
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"ContentChangesHash",
      .callback =
          [] {
            CHECK(std::hash<Line>{}(
                      LineBuilder(NewLazyString(L"alejo")).Build().value()) !=
                  std::hash<Line>{}(
                      LineBuilder(NewLazyString(L"Xlejo")).Build().value()));
          }},
     {.name = L"ModifiersChangesHash",
      .callback =
          [] {
            LineBuilder options(NewLazyString(L"alejo"));
            size_t initial_hash =
                std::hash<Line>{}(options.Copy().Build().value());
            options.InsertModifier(ColumnNumber(2), LineModifier::kRed);
            size_t final_hash =
                std::hash<Line>{}(std::move(options).Build().value());
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"MetadataBecomesAvailable", .callback = [] {
        futures::Future<LazyString> future;
        LineBuilder builder;
        builder.SetMetadata(
            LineMetadataEntry{.initial_value = NewLazyString(L"Foo"),
                              .value = std::move(future.value)});
        NonNull<std::shared_ptr<Line>> line = std::move(builder).Build();
        CHECK(line->metadata()->ToString() == L"Foo");
        std::move(future.consumer)(NewLazyString(L"Bar"));
        CHECK(line->metadata()->ToString() == L"Bar");
      }}});
}

LineBuilder::LineBuilder(const Line& line) : data_(line.data_) {}

LineBuilder::LineBuilder(language::lazy_string::LazyString input_contents)
    : data_(Line::Data{.contents = std::move(input_contents)}) {}

LineBuilder::LineBuilder(Line::Data data) : data_(std::move(data)) {}

LineBuilder LineBuilder::Copy() const { return LineBuilder(data_); }

NonNull<std::shared_ptr<Line>> LineBuilder::Build() && {
  return MakeNonNullShared<Line>(Line(std::move(data_)));
}

ColumnNumber LineBuilder::EndColumn() const {
  // TODO: Compute this separately, taking the width of characters into
  // account.
  return ColumnNumber(0) + contents().size();
}

language::lazy_string::ColumnNumberDelta LineBuilder::size() const {
  return contents().size();
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
    LazyString suffix =
        lazy_string::Substring(data_.contents, column + ColumnNumberDelta(1));
    data_.contents = lazy_string::Append(
        lazy_string::Substring(std::move(data_.contents), ColumnNumber(0),
                               column.ToDelta()),
        std::move(str), std::move(suffix));
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
        CHECK(options.contents().ToString() == L"ALEJANDRO");
        CHECK(options.modifiers().empty());

        options.SetCharacter(ColumnNumber(1), L'l',
                             LineModifierSet{LineModifier::kBold});
        CHECK(options.contents().ToString() == L"AlEJANDRO");
        CHECK_EQ(options.modifiers().size(), 2ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(2))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(2), L'e',
                             LineModifierSet{LineModifier::kBold});
        CHECK(options.contents().ToString() == L"AleJANDRO");
        CHECK_EQ(options.modifiers().size(), 2ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(3), L'j',
                             LineModifierSet{LineModifier::kUnderline});
        CHECK(options.contents().ToString() == L"AlejANDRO");
        CHECK_EQ(options.modifiers().size(), 3ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 LineModifierSet{LineModifier::kBold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 LineModifierSet{LineModifier::kUnderline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second,
                 LineModifierSet{});

        options.SetCharacter(ColumnNumber(5), L'n',
                             LineModifierSet{LineModifier::kBlue});
        CHECK(options.contents().ToString() == L"AlejAnDRO");
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
        CHECK(options.contents().ToString() == L"AlejanDRO");
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
        CHECK(options.contents().ToString() == L"alejanDRO");
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
  CHECK(!modifier.contains(LineModifier::kReset));
  data_.modifiers[ColumnNumber(0) + data_.contents.size()] = modifier;
  data_.contents = lazy_string::Append(std::move(data_.contents),
                                       NewLazyString(std::wstring(1, c)));
  SetMetadata(std::nullopt);
  ValidateInvariants();
}

void LineBuilder::AppendString(LazyString suffix) {
  AppendString(std::move(suffix), std::nullopt);
}

void LineBuilder::AppendString(
    LazyString suffix, std::optional<LineModifierSet> suffix_modifiers) {
  ValidateInvariants();
  LineBuilder suffix_line(std::move(suffix));
  if (suffix_modifiers.has_value() &&
      suffix_line.data_.contents.size() > ColumnNumberDelta(0)) {
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

void LineBuilder::SetOutgoingLink(OutgoingLink outgoing_link) {
  data_.outgoing_link = outgoing_link;
}

std::optional<OutgoingLink> LineBuilder::outgoing_link() const {
  return data_.outgoing_link;
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

LineBuilder& LineBuilder::set_end_of_line_modifiers(LineModifierSet values) {
  data_.end_of_line_modifiers = std::move(values);
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
bool LineBuilder::modifiers_empty() const { return data_.modifiers.empty(); }

std::pair<language::lazy_string::ColumnNumber, LineModifierSet>
LineBuilder::modifiers_last() const {
  return *data_.modifiers.rbegin();
}

void LineBuilder::InsertModifier(language::lazy_string::ColumnNumber position,
                                 LineModifier modifier) {
  data_.modifiers[position].insert(modifier);
}
void LineBuilder::InsertModifiers(language::lazy_string::ColumnNumber position,
                                  const LineModifierSet& modifiers) {
  data_.modifiers[position].insert(modifiers.begin(), modifiers.end());
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

LazyString LineBuilder::contents() const { return data_.contents; }

void LineBuilder::set_contents(LazyString value) {
  data_.contents = std::move(value);
}

void LineBuilder::ValidateInvariants() {}
}  // namespace afc::language::text
