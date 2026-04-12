#include "src/file_open_position.h"

#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"
#include "src/language/text/line.h"
#include "src/tests/tests.h"

using afc::infrastructure::Path;
using afc::language::IgnoreErrors;
using afc::language::OptionalFrom;
using afc::language::overload;
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
std::optional<Spec> TryPosition(LazyString input) {
  std::vector<LazyString> tokens = SplitAt(input, L':');
  if (tokens.size() < 1 || tokens.size() > 2) return std::nullopt;
  LineColumn position = {};
  for (size_t i = 0; i < tokens.size(); ++i) {
    size_t value;
    try {
      value = stoi(tokens[i].ToString());
      if (value > 0) {
        value--;
      }
    } catch (const std::invalid_argument& ia) {
      LOG(INFO) << "stoi failed: invalid argument: " << tokens[i];
      return std::nullopt;
    } catch (const std::out_of_range& ia) {
      LOG(INFO) << "stoi failed: out of range: " << tokens[i];
      return std::nullopt;
    }
    if (i == 0) {
      position.line = LineNumber(value);
    } else {
      position.column = ColumnNumber(value);
    }
  }
  return position;
}

std::optional<Spec> TrySearchPattern(LazyString input) {
  if (!StartsWith(input, LazyString{L"/"})) return std::nullopt;
  if (std::optional<NonEmptySingleLine> pattern =
          OptionalFrom(NonEmptySingleLine::New(
              SingleLine::New(input.Substring(ColumnNumber{1}))));
      pattern.has_value()) {
    return Search{pattern.value()};
  }
  return std::nullopt;
}
}  // namespace

std::optional<Spec> Parse(language::lazy_string::LazyString path_suffix) {
  if (path_suffix.empty()) return Default{};
  if (path_suffix.get(ColumnNumber{0}) != L':') return std::nullopt;
  LazyString input = path_suffix.Substring(ColumnNumber{1});
  if (std::optional<Spec> search_candidate = TrySearchPattern(input);
      search_candidate.has_value())
    return search_candidate.value();
  if (std::optional<Spec> position_candidate = TryPosition(input);
      position_candidate.has_value())
    return position_candidate.value();
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

Spec SpecFromLineMetadata(const language::text::LineMetadataMap&) {
  // TODO(P1, trivial): Implement.
  return Default{};
}

}  // namespace afc::editor::file_open_position
