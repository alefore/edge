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
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::infrastructure::screen::Color;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::MakeNonNullShared;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::language::text {

namespace {
const bool line_tests_registration = tests::Register(
    L"LineTests",
    {{.name = L"ConstructionOfEmptySetsEndOfLine",
      .callback =
          [] {
            LineBuilder options;
            options.insert_end_of_line_modifiers(
                Style{.foreground_color = Color::Red});
            CHECK_EQ(std::move(options)
                         .Build()
                         .end_of_line_modifiers()
                         .foreground_color.value(),
                     Color::Red);
          }},
     {.name = L"CopyOfEmptyPreservesEndOfLine",
      .callback =
          [] {
            LineBuilder options;
            options.insert_end_of_line_modifiers(
                Style{.foreground_color = Color::Red});
            Line initial_line = std::move(options).Build();
            Line final_line(std::move(initial_line));
            CHECK_EQ(
                final_line.end_of_line_modifiers().foreground_color.value(),
                Color::Red);
          }},
     {.name = L"EndOfLineModifiersChangesHash",
      .callback =
          [] {
            LineBuilder options;
            size_t initial_hash = std::hash<Line>{}(options.Copy().Build());
            options.insert_end_of_line_modifiers(
                Style{.foreground_color = Color::Red});
            size_t final_hash = std::hash<Line>{}(std::move(options).Build());
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"ContentChangesHash",
      .callback =
          [] {
            CHECK(std::hash<Line>{}(
                      LineBuilder{SINGLE_LINE_CONSTANT(L"alejo")}.Build()) !=
                  std::hash<Line>{}(
                      LineBuilder{SINGLE_LINE_CONSTANT(L"Xlejo")}.Build()));
          }},
     {.name = L"ModifiersChangesHash",
      .callback =
          [] {
            LineBuilder options{SINGLE_LINE_CONSTANT(L"alejo")};
            size_t initial_hash = std::hash<Line>{}(options.Copy().Build());
            options.InsertModifiers(ColumnNumber(2),
                                    Style{.foreground_color = Color::Red});
            size_t final_hash = std::hash<Line>{}(std::move(options).Build());
            CHECK(initial_hash != final_hash);
          }},
     {.name = L"MetadataIsNotEvaluatedOnConstruction",
      .callback =
          [] {
            LineBuilder builder;
            builder.SetMetadata(LazyValue<LineMetadataMap>([] {
              LOG(FATAL) << "Evaluated metadata!";
              return LineMetadataMap{};
            }));
            CHECK(!std::move(builder).Build().metadata().has_value());
          }},
     {.name = L"MetadataBecomesAvailable", .callback = [] {
        futures::Future<SingleLine> future;
        LineBuilder builder;
        const LineMetadataKey key;
        builder.SetMetadata(WrapAsLazyValue(
            LineMetadataMap{{{key, futures::Progressive<SingleLine>(
                                       SINGLE_LINE_CONSTANT(L"Foo"),
                                       std::move(future.value))}}}));
        Line line = std::move(builder).Build();
        CHECK(line.metadata().get().at(key).current() == LazyString{L"Foo"});
        std::move(future.consumer)(SINGLE_LINE_CONSTANT(L"Bar"));
        CHECK(line.metadata().get().at(key).current() == LazyString{L"Bar"});
      }}});

const bool line_modifiers_at_position_tests_registration = tests::Register(
    L"LineModifiersAtPosition",
    {{.name = L"EmptyModifiers",
      .callback =
          [] {
            Line line = LineBuilder{SINGLE_LINE_CONSTANT(L"alejo")}.Build();
            CHECK(line.modifiers_at_position(ColumnNumber{}).empty());
            CHECK(line.modifiers_at_position(ColumnNumber{3}).empty());
            CHECK(line.modifiers_at_position(ColumnNumber{999}).empty());
          }},
     {.name = L"ExactMatch",
      .callback =
          [] {
            LineBuilder builder{SINGLE_LINE_CONSTANT(L"alejandro")};
            builder.InsertModifiers(ColumnNumber(2),
                                    Style{.foreground_color = Color::Red});
            builder.InsertModifiers(ColumnNumber(5),
                                    Style{.foreground_color = Color::Blue});
            Line line = std::move(builder).Build();
            CHECK(line.modifiers_at_position(ColumnNumber{2}) ==
                  Style{Color::Red});
            CHECK(line.modifiers_at_position(ColumnNumber{5}) ==
                  Style{Color::Blue});
          }},
     {.name = L"PositionBeforeModifiers",
      .callback =
          [] {
            LineBuilder builder{SINGLE_LINE_CONSTANT(L"alejandro")};
            builder.InsertModifiers(ColumnNumber(5),
                                    Style{.foreground_color = Color::Blue});
            CHECK(std::move(builder)
                      .Build()
                      .modifiers_at_position(ColumnNumber{2})
                      .empty());
          }},
     {.name = L"InexactMatch",
      .callback =
          [] {
            LineBuilder builder{SINGLE_LINE_CONSTANT(L"alejandro")};
            builder.InsertModifiers(ColumnNumber(2),
                                    Style{.foreground_color = Color::Green});
            builder.InsertModifiers(ColumnNumber(5),
                                    Style{.foreground_color = Color::Blue});
            CHECK(std::move(builder).Build().modifiers_at_position(
                      ColumnNumber{4}) == Style{Color::Green});
          }},
     {.name = L"InexactMatchAfterLast", .callback = [] {
        LineBuilder builder{SINGLE_LINE_CONSTANT(L"alejandro")};
        builder.InsertModifiers(ColumnNumber(5),
                                Style{.foreground_color = Color::Blue});
        CHECK(std::move(builder).Build().modifiers_at_position(
                  ColumnNumber{8}) == Style{Color::Blue});
      }}});
}  // namespace

LineBuilder::LineBuilder(const Line& line) : data_(line.data_.value()) {}

LineBuilder::LineBuilder(SingleLine input_contents)
    : data_(Line::Data{.contents = std::move(input_contents)}) {}

LineBuilder::LineBuilder(language::lazy_string::SingleLine input_contents,
                         afc::infrastructure::screen::Style modifiers)
    : data_(Line::Data{.contents = std::move(input_contents),
                       .modifiers = {{ColumnNumber{}, std::move(modifiers)}}}) {
}

LineBuilder::LineBuilder(NonEmptySingleLine input_contents)
    : LineBuilder(input_contents.read()) {}

LineBuilder::LineBuilder(Line::Data data) : data_(std::move(data)) {}

LineBuilder LineBuilder::Copy() const { return LineBuilder(data_); }

Line LineBuilder::Build() && {
  data_.escaped_map_supplier =
      MakeLazyValue(std::bind_front(vm::EscapedMap::Parse, data_.contents));
  return Line(MakeNonNullShared<const Line::Data>(std::move(data_)));
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
                               const Style& c_modifiers) {
  ValidateInvariants();
  VLOG(4) << "Start SetCharacter: " << column;
  SingleLine str{LazyString{ColumnNumberDelta{1}, c}};
  if (column >= EndColumn()) {
    column = EndColumn();
    data_.contents = std::move(data_.contents).Append(std::move(str));
  } else {
    SingleLine suffix = data_.contents.Substring(column + ColumnNumberDelta(1));
    data_.contents = std::move(data_.contents)
                         .Substring(ColumnNumber(0), column.ToDelta())
                         .Append(std::move(str))
                         .Append(std::move(suffix));
  }

  data_.metadata = WrapAsLazyValue(LineMetadataMap{});

  // Return the modifiers that are effective at a given position.
  auto Read = [&](ColumnNumber position) -> Style {
    if (data_.modifiers.empty() || data_.modifiers.begin()->first > position)
      return {};
    auto it = data_.modifiers.lower_bound(position);
    if (it == data_.modifiers.end() || it->first > position) --it;
    return it->second;
  };

  auto Set = [&](ColumnNumber position, Style value) {
    Style previous_value =
        position.IsZero() ? Style{} : Read(position - ColumnNumberDelta(1));
    if (previous_value == value)
      data_.modifiers.erase(position);
    else
      data_.modifiers[position] = value;
  };

  ColumnNumber after_column = column + ColumnNumberDelta(1);
  Style modifiers_after_column = Read(after_column);

  Set(column, c_modifiers);

  if (after_column < EndColumn()) Set(after_column, modifiers_after_column);

  ValidateInvariants();

  for (std::pair<ColumnNumber, Style> entry : data_.modifiers)
    VLOG(5) << "Modifiers: " << entry.first << ": " << entry.second;
}

namespace {
const bool line_set_character_tests_registration = tests::Register(
    L"LineTestsSetCharacter",
    {{.name = L"ConsecutiveSets", .callback = [] {
        LineBuilder options;
        options.AppendString(SINGLE_LINE_CONSTANT(L"ALEJANDRO"), std::nullopt);
        CHECK_EQ(options.contents(), SINGLE_LINE_CONSTANT(L"ALEJANDRO"));
        CHECK(options.modifiers().empty());

        options.SetCharacter(ColumnNumber(1), L'l',
                             Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.contents(), SINGLE_LINE_CONSTANT(L"AlEJANDRO"));
        CHECK_EQ(options.modifiers().size(), 2ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(2))->second, Style{});

        options.SetCharacter(ColumnNumber(2), L'e',
                             Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.contents(), SINGLE_LINE_CONSTANT(L"AleJANDRO"));
        CHECK_EQ(options.modifiers().size(), 2ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second, Style{});

        options.SetCharacter(ColumnNumber(3), L'j',
                             Style{.attributes = StyleAttribute::Underline});
        CHECK_EQ(options.contents(), SINGLE_LINE_CONSTANT(L"AlejANDRO"));
        CHECK_EQ(options.modifiers().size(), 3ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 Style{.attributes = StyleAttribute::Underline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second, Style{});

        options.SetCharacter(ColumnNumber(5), L'n', Style{Color::Blue});
        CHECK_EQ(options.contents(), SINGLE_LINE_CONSTANT(L"AlejAnDRO"));
        CHECK_EQ(options.modifiers().size(), 5ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 Style{.attributes = StyleAttribute::Underline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second, Style{});
        CHECK_EQ(options.modifiers().find(ColumnNumber(5))->second,
                 Style{Color::Blue});
        CHECK_EQ(options.modifiers().find(ColumnNumber(6))->second, Style{});

        options.SetCharacter(ColumnNumber(4), L'a', Style{Color::Red});
        CHECK_EQ(options.contents(), SINGLE_LINE_CONSTANT(L"AlejanDRO"));
        CHECK_EQ(options.modifiers().size(), 5ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(1))->second,
                 Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 Style{.attributes = StyleAttribute::Underline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second,
                 Style{Color::Red});
        CHECK_EQ(options.modifiers().find(ColumnNumber(5))->second,
                 Style{Color::Blue});
        CHECK_EQ(options.modifiers().find(ColumnNumber(6))->second, Style{});

        options.SetCharacter(ColumnNumber(0), L'a',
                             Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.contents(), SINGLE_LINE_CONSTANT(L"alejanDRO"));
        CHECK_EQ(options.modifiers().size(), 5ul);
        CHECK_EQ(options.modifiers().find(ColumnNumber(0))->second,
                 Style{.attributes = StyleAttribute::Bold});
        CHECK_EQ(options.modifiers().find(ColumnNumber(3))->second,
                 Style{.attributes = StyleAttribute::Underline});
        CHECK_EQ(options.modifiers().find(ColumnNumber(4))->second,
                 Style{Color::Red});
        CHECK_EQ(options.modifiers().find(ColumnNumber(5))->second,
                 Style{Color::Blue});
        CHECK_EQ(options.modifiers().find(ColumnNumber(6))->second, Style{});
      }}});
}  // namespace

void LineBuilder::InsertCharacterAtPosition(ColumnNumber column) {
  ValidateInvariants();
  set_contents(data_.contents.Substring(ColumnNumber(0), column.ToDelta()) +
               SingleLine{LazyString{L" "}} + data_.contents.Substring(column));

  std::map<ColumnNumber, Style> new_modifiers;
  for (auto& m : data_.modifiers) {
    new_modifiers[m.first + (m.first < column ? ColumnNumberDelta(0)
                                              : ColumnNumberDelta(1))] =
        std::move(m.second);
  }
  set_modifiers(std::move(new_modifiers));
  SetMetadata(WrapAsLazyValue(LineMetadataMap{}));
  ValidateInvariants();
}

void LineBuilder::AppendCharacter(wchar_t c, Style modifier) {
  ValidateInvariants();
  data_.modifiers[ColumnNumber(0) + data_.contents.size()] = modifier;
  data_.contents = std::move(data_.contents) +
                   SingleLine{LazyString{ColumnNumberDelta{1}, c}};
  SetMetadata(WrapAsLazyValue(LineMetadataMap{}));
  ValidateInvariants();
}

void LineBuilder::AppendString(SingleLine suffix) {
  AppendString(std::move(suffix), std::nullopt);
}

void LineBuilder::AppendString(SingleLine suffix,
                               std::optional<Style> suffix_modifiers) {
  ValidateInvariants();
  LineBuilder suffix_line(std::move(suffix));
  if (suffix_modifiers.has_value() &&
      suffix_line.data_.contents.size() > ColumnNumberDelta(0)) {
    suffix_line.data_.modifiers[ColumnNumber(0)] = suffix_modifiers.value();
  }
  Append(std::move(suffix_line));
  ValidateInvariants();
}

void LineBuilder::Append(LineBuilder line) {
  ValidateInvariants();
  data_.end_of_line_modifiers = std::move(line.data_.end_of_line_modifiers);
  if (line.EndColumn().IsZero()) return;
  ColumnNumberDelta original_length = EndColumn().ToDelta();
  data_.contents =
      std::move(data_.contents).Append(std::move(line.data_.contents));
  SetMetadata(WrapAsLazyValue(LineMetadataMap{}));

  auto initial_modifier =
      line.data_.modifiers.empty() ||
              line.data_.modifiers.begin()->first != ColumnNumber(0)
          ? Style{}
          : line.data_.modifiers.begin()->second;
  auto final_modifier =
      data_.modifiers.empty() ? Style{} : data_.modifiers.rbegin()->second;
  if (initial_modifier != final_modifier) {
    data_.modifiers[ColumnNumber() + original_length] = initial_modifier;
  }
  for (auto& [position, new_modifiers] : line.data_.modifiers) {
    if ((data_.modifiers.empty()
             ? Style{}
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

LineBuilder& LineBuilder::DeleteCharacters(ColumnNumber column,
                                           ColumnNumberDelta delta) {
  ValidateInvariants();
  CHECK_GE(delta, ColumnNumberDelta(0));
  CHECK_LE(column, EndColumn());
  CHECK_LE(column + delta, EndColumn());

  data_.contents = data_.contents.Substring(ColumnNumber(0), column.ToDelta())
                       .Append(data_.contents.Substring(column + delta));

  std::map<ColumnNumber, Style> new_modifiers;
  // TODO: We could optimize this to only set it once (rather than for every
  // modifier before the deleted range).
  std::optional<Style> last_modifiers_before_gap;
  std::optional<Style> modifiers_continuation;
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
  SetMetadata(WrapAsLazyValue(LineMetadataMap{}));

  ValidateInvariants();
  return *this;
}

LineBuilder& LineBuilder::DeleteSuffix(ColumnNumber column) {
  if (column >= EndColumn()) return *this;
  return DeleteCharacters(column, EndColumn() - column);
}

LineBuilder& LineBuilder::SetAllModifiers(Style value) {
  set_modifiers({{ColumnNumber(0), value}});
  data_.end_of_line_modifiers = std::move(value);
  return *this;
}

LineBuilder& LineBuilder::insert_end_of_line_modifiers(Style values) {
  data_.end_of_line_modifiers.Merge(values);
  return *this;
}

LineBuilder& LineBuilder::set_end_of_line_modifiers(Style values) {
  data_.end_of_line_modifiers = std::move(values);
  return *this;
}

Style LineBuilder::copy_end_of_line_modifiers() const {
  return data_.end_of_line_modifiers;
}

std::map<language::lazy_string::ColumnNumber, Style> LineBuilder::modifiers()
    const {
  return data_.modifiers;
}

size_t LineBuilder::modifiers_size() const { return data_.modifiers.size(); }
bool LineBuilder::modifiers_empty() const { return data_.modifiers.empty(); }

std::pair<language::lazy_string::ColumnNumber, Style>
LineBuilder::modifiers_last() const {
  return *data_.modifiers.rbegin();
}

#if 0
void LineBuilder::InsertModifier(language::lazy_string::ColumnNumber position,
                                 LineModifier modifier) {
  data_.modifiers[position].insert(modifier);
}
#endif

void LineBuilder::InsertModifiers(language::lazy_string::ColumnNumber position,
                                  const Style& modifiers) {
  data_.modifiers[position].Merge(modifiers);
}

void LineBuilder::set_modifiers(language::lazy_string::ColumnNumber position,
                                Style value) {
  data_.modifiers[position] = std::move(value);
}

void LineBuilder::set_modifiers(
    std::map<language::lazy_string::ColumnNumber, Style> value) {
  data_.modifiers = std::move(value);
}

void LineBuilder::ClearModifiers() { data_.modifiers.clear(); }

SingleLine LineBuilder::contents() const { return data_.contents; }

void LineBuilder::set_contents(SingleLine value) {
  data_.contents = std::move(value);
}

void LineBuilder::InternalSetMetadata(LazyValue<LineMetadataMap> metadata) {
  data_.metadata = std::move(metadata);
}

void LineBuilder::ValidateInvariants() {}
}  // namespace afc::language::text
