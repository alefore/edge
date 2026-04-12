#include "src/file_open_position.h"

#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"
#include "src/language/text/line.h"
#include "src/tests/tests.h"

using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::OptionalFrom;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::SplitAt;
using afc::language::text::LineColumn;
using afc::language::text::LineMetadataKey;
using afc::language::text::LineMetadataMap;
using afc::language::text::LineMetadataValue;
using afc::language::text::LineNumber;

namespace afc::editor::file_open_position {
bool operator==(const Default&, const Default&) { return true; }

std::ostream& operator<<(std::ostream& os, const Default&) {
  os << "<default>";
  return os;
}

std::ostream& operator<<(std::ostream& os, const Spec& spec) {
  std::visit([&os](auto&& arg) { os << arg; }, spec);
  return os;
}

namespace {
// TODO(P1, trivial, 2026-04-12): Move to a central location.
ValueOrError<int> AsNumber(LazyString value) {
  try {
    return stoi(value.ToString());
  } catch (const std::invalid_argument& ia) {
    return Error{LazyString{L"stoi failed: invalid argument: "} + value};
  } catch (const std::out_of_range& ia) {
    return Error{LazyString{L"stoi failed: out of range: "} + value};
  }
}

ValueOrError<Spec> TryPosition(LazyString input) {
  std::vector<LazyString> tokens = SplitAt(input, L':');
  if (tokens.size() < 1 || tokens.size() > 2)
    return Error{LazyString{L"Unexpected number of tokens"}};
  LineColumn position = {};
  for (size_t i = 0; i < tokens.size(); ++i) {
    DECLARE_OR_RETURN(int value, AsNumber(tokens[i]));
    if (value > 0) value--;
    if (i == 0) {
      position.line = LineNumber(value);
    } else {
      position.column = ColumnNumber(value);
    }
  }
  return position;
}

ValueOrError<Spec> TrySearchPattern(LazyString input) {
  if (!StartsWith(input, LazyString{L"/"}))
    return Error{LazyString{L"Input doesn't start with slash."}};
  DECLARE_OR_RETURN(NonEmptySingleLine pattern,
                    NonEmptySingleLine::New(
                        SingleLine::New(input.Substring(ColumnNumber{1}))));
  return Search{pattern};
}
}  // namespace

std::optional<Spec> Parse(language::lazy_string::LazyString path_suffix) {
  if (path_suffix.empty()) return Default{};
  if (path_suffix.get(ColumnNumber{0}) != L':') return std::nullopt;
  LazyString input = path_suffix.Substring(ColumnNumber{1});
  if (ValueOrError<Spec> search_candidate = TrySearchPattern(input);
      std::holds_alternative<Spec>(search_candidate))
    return std::get<Spec>(search_candidate);
  if (ValueOrError<Spec> position_candidate = TryPosition(input);
      std::holds_alternative<Spec>(position_candidate))
    return std::get<Spec>(position_candidate);
  LOG(INFO) << "Invalid parse: " << path_suffix;
  return std::nullopt;
}

namespace {
const bool parse_path_spec_tests_registration = tests::Register(
    L"file_open_position_Parse",
    {{.name = L"NoParse",
      .callback = [] { CHECK(Parse(LazyString{L"foo"}) == std::nullopt); }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(std::holds_alternative<Default>(Parse(LazyString{}).value()));
          }},
     {.name = L"Pattern",
      .callback =
          [] {
            CHECK_EQ(Parse(LazyString{L":/quux"}).value(),
                     Spec{Search{NON_EMPTY_SINGLE_LINE_CONSTANT(L"quux")}});
          }},
     {.name = L"LineColumn",
      .callback =
          [] {
            CHECK_EQ(Parse(LazyString{L":25:43"}).value(),
                     (Spec{LineColumn{LineNumber{24}, ColumnNumber{42}}}));
          }},
     {.name = L"Line", .callback = [] {
        CHECK_EQ(Parse(LazyString{L":2543"}).value(),
                 Spec{LineColumn{LineNumber{2542}}});
      }}});

const LineMetadataKey kLineKey = LineMetadataKey{SINGLE_LINE_CONSTANT(L"line")};
}  // namespace

LineMetadataMap GetLineMetadata(Spec spec) {
  return std::visit(
      overload{
          [](Default) { return LineMetadataMap{}; },
          [](Search search) {
            return LineMetadataMap{
                {{LineMetadataKey{SINGLE_LINE_CONSTANT(L"search")},
                  LineMetadataValue::FromSingleLine(search.read().read())}}};
          },
          [](LineColumn position) {
            // TODO(P1, trivial, 2026-04-12): Also handle column.
            return LineMetadataMap{
                {{kLineKey,
                  LineMetadataValue::FromSingleLine(
                      NonEmptySingleLine(position.line.read()).read())}}};
          },
      },
      spec);
}

ValueOrError<Spec> SpecFromLineMetadataInternal(
    const language::text::LineMetadataMap& values) {
  if (auto it = values.find(kLineKey); it != values.end()) {
    DECLARE_OR_RETURN(int line_int,
                      AsNumber(ToLazyString(it->second.get_value())));
    return LineColumn{LineNumber{static_cast<size_t>(line_int)}};
  }
  return Default{};
}

Spec SpecFromLineMetadata(const language::text::LineMetadataMap& values) {
  return OptionalFrom(SpecFromLineMetadataInternal(values)).value_or(Default{});
}

}  // namespace afc::editor::file_open_position
