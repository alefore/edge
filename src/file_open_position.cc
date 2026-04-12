#include "src/file_open_position.h"

#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"
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
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;

namespace afc::editor::file_open_position {
namespace {
std::optional<PathAndSpec> TryPosition(Path path,
                                       std::vector<LazyString> inputs) {
  if (inputs.size() != 1 && inputs.size() != 2) return std::nullopt;
  LineColumn position = {};
  for (size_t i = 0; i < inputs.size(); ++i) {
    size_t value;
    try {
      value = stoi(inputs[i].ToString());
      if (value > 0) {
        value--;
      }
    } catch (const std::invalid_argument& ia) {
      LOG(INFO) << "stoi failed: invalid argument: " << inputs[i];
      return std::nullopt;
    } catch (const std::out_of_range& ia) {
      LOG(INFO) << "stoi failed: out of range: " << inputs[i];
      return std::nullopt;
    }
    if (i == 0) {
      position.line = LineNumber(value);
    } else {
      position.column = ColumnNumber(value);
    }
  }
  return PathAndSpec{.path = path, .spec = position};
}

std::optional<PathAndSpec> TrySearchPattern(Path path,
                                            std::vector<LazyString> inputs) {
  if (inputs.size() != 1 || !StartsWith(inputs[0], LazyString{L"/"}))
    return std::nullopt;
  if (std::optional<NonEmptySingleLine> pattern =
          OptionalFrom(NonEmptySingleLine::New(
              SingleLine::New(inputs[0].Substring(ColumnNumber{1}))));
      pattern.has_value()) {
    return PathAndSpec{.path = path, .spec = Search{pattern.value()}};
  }
  return std::nullopt;
}
}  // namespace

std::vector<PathAndSpec> Parse(language::lazy_string::LazyString full_path) {
  // We gradually peel off suffixes from input_path into suffixes. As we do
  // this, we append entries to output.
  std::vector<PathAndSpec> output;
  ColumnNumberDelta str_end = full_path.size();
  std::vector<LazyString> suffixes = {};
  while (true) {
    std::visit(
        overload{IgnoreErrors{},
                 [&](Path path) {
                   if (suffixes.empty())
                     output.push_back({.path = path, .spec = Default{}});
                   else if (std::optional<PathAndSpec> search_candidate =
                                TrySearchPattern(path, suffixes);
                            search_candidate.has_value())
                     output.push_back(search_candidate.value());
                   else if (std::optional<PathAndSpec> position_candidate =
                                TryPosition(path, suffixes);
                            position_candidate.has_value())
                     output.push_back(position_candidate.value());
                   else
                     LOG(INFO) << "Invalid parse: " << path << ": "
                               << suffixes.size() << ": " << full_path;
                 }},
        Path::New(full_path.Substring(ColumnNumber{}, str_end)));
    if (suffixes.size() == 2) {
      CHECK(!output.empty());
      return output;
    }
    std::optional<ColumnNumber> new_colon = FindLastOf(
        full_path, {L':'}, ColumnNumber{} + str_end - ColumnNumberDelta{1});
    if (new_colon == std::nullopt) {
      CHECK(!output.empty());
      return output;
    }
    ColumnNumberDelta new_colon_delta = new_colon->ToDelta();
    CHECK_LT(new_colon_delta, str_end);
    suffixes.insert(suffixes.begin(),
                    full_path.Substring(
                        new_colon.value() + ColumnNumberDelta{1},
                        str_end - (new_colon_delta + ColumnNumberDelta{1})));
    str_end = new_colon_delta;
  }
}

namespace {
const bool parse_path_spec_tests_registration = tests::Register(
    L"file_open_position_Parse",
    {{.name = L"Simple",
      .callback =
          [] {
            auto output = Parse(LazyString{L"foo/bar"});
            CHECK_EQ(output.size(), 1ul);
            CHECK_EQ(output[0].path,
                     ValueOrDie(Path::New(LazyString{L"foo/bar"})));
            CHECK(std::holds_alternative<Default>(output[0].spec));
          }},
     {.name = L"Pattern",
      .callback =
          [] {
            auto output = Parse(LazyString{L"foo/bar:/quux"});
            CHECK_EQ(output.size(), 2ul);
            CHECK_EQ(output[1].path,
                     ValueOrDie(Path::New(LazyString{L"foo/bar"})));
            CHECK_EQ(std::get<Search>(output[1].spec),
                     Search{NON_EMPTY_SINGLE_LINE_CONSTANT(L"quux")});
          }},
     {.name = L"Multiple", .callback = [] {
        auto output = Parse(LazyString{L"foo/bar:quux::meh:25:43"});
        CHECK_EQ(output.size(), 3ul);

        CHECK_EQ(output[0].path,
                 ValueOrDie(Path::New(LazyString{L"foo/bar:quux::meh:25:43"})));
        CHECK(std::holds_alternative<Default>(output[0].spec));

        CHECK_EQ(output[1].path,
                 ValueOrDie(Path::New(LazyString{L"foo/bar:quux::meh:25"})));
        CHECK_EQ(std::get<LineColumn>(output[1].spec),
                 LineColumn{LineNumber{42}});

        CHECK_EQ(output[2].path,
                 ValueOrDie(Path::New(LazyString{L"foo/bar:quux::meh"})));
        CHECK_EQ(std::get<LineColumn>(output[2].spec),
                 (LineColumn{LineNumber{24}, ColumnNumber{42}}));
      }}});
}  // namespace
}  // namespace afc::editor::file_open_position
