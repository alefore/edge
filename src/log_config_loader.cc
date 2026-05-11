#include "src/log_config_loader.h"

#include <expected>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/view.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/convert.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/text/line.h"
#include "src/open_files.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::futures::UnwrapVectorFuture;
using afc::infrastructure::Path;
using afc::language::AugmentError;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::PossibleError;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::lazy_string::Trim;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;
using afc::language::view::GetErrors;
using afc::language::view::SkipErrors;
using afc::vm::Identifier;

namespace afc::editor {
namespace {
static const std::unordered_set<wchar_t> Space = {L' '};
}

auto GetBlockIndices(LineSequence lines) {
  return lines | std::views::enumerate |
         std::views::filter([](std::tuple<size_t, const Line&> input) {
           SingleLine line = Trim(std::get<1>(input).contents(), Space);
           return StartsWith(line, LazyString{L"["}) &&
                  EndsWith(line, LazyString{L"]"});
         }) |
         std::views::transform([](std::tuple<size_t, const Line&> input) {
           return LineColumn{LineNumber{std::get<0>(input)}};
         });
}

std::vector<LineSequence> PartitionIntoBlocks(LineSequence lines) {
  // TODO(P2, C++26, 2026-04-28): Avoid the conversion to vector once we can
  // use std::views::concat:
  //    std::views::concat(GetBlockIndices(lines),
  //                       std::views::single(LineNumber{} + lines.size())
  std::vector<LineColumn> indices =
      GetBlockIndices(lines) | std::ranges::to<std::vector>();
  indices.push_back(lines.range().end());
  return indices | std::views::adjacent<2> |
         std::views::transform(
             [lines](std::tuple<LineColumn, LineColumn> entry) {
               Range range{std::get<0>(entry), std::get<1>(entry)};
               CHECK_LT(range.begin().line, range.end().line);
               return lines.ViewRange(range);
             }) |
         std::ranges::to<std::vector>();
}

struct SectionHeader {
  NonEmptySingleLine header_type;
  NonEmptySingleLine value;
};

std::ostream& operator<<(std::ostream& os, const SectionHeader& value) {
  os << "[" << value.header_type << " " << value.value << "]";
  return os;
}

bool operator==(const SectionHeader& a, const SectionHeader& b) {
  return a.header_type == b.header_type && a.value == b.value;
}

std::expected<SectionHeader, Error> ParseSectionHeader(SingleLine header) {
  header = Trim(header, Space);
  if (!StartsWith(header, SINGLE_LINE_CONSTANT(L"[")))
    return Error{L"Section header must start with ["};
  std::optional<ColumnNumber> header_type_start =
      FindFirstNotOf(header, Space, ColumnNumber{1});
  std::optional<ColumnNumber> header_type_end = header_type_start.and_then(
      [&](ColumnNumber pos) { return FindFirstOf(header, Space, pos); });
  std::optional<ColumnNumber> value_start = header_type_end.and_then(
      [&](ColumnNumber pos) { return FindFirstNotOf(header, Space, pos); });
  std::optional<ColumnNumber> value_end = value_start.and_then(
      [&](ColumnNumber pos) { return FindFirstOf(header, {L' ', L']'}, pos); });
  if (!value_end)
    return Error{LazyString{L"Invalid section header: "} + header};
  if (Trim(header.Substring(value_end.value()), Space) != L"]")
    return Error{LazyString{L"Invalid section header (end): "} + header};
  VLOG(9) << "ParseSectionHeader: " << header_type_start.value() << " "
          << header_type_end.value() << " " << value_start.value() << " "
          << value_end.value();
  auto header_type = NonEmptySingleLine::New(
      header.Substring(header_type_start.value(),
                       header_type_end.value() - header_type_start.value()));
  auto value = NonEmptySingleLine::New(header.Substring(
      value_start.value(), value_end.value() - value_start.value()));
  if (!header_type || !value)
    return Error{LazyString{L"Invalid section header (extract): "} + header};
  return SectionHeader{.header_type = header_type.value(),
                       .value = value.value()};
}

namespace {
const bool parse_section_header_tests_registration = tests::Register(
    L"LogModelParseSectionHeader", std::invoke([] -> std::vector<tests::Test> {
      auto err = [](std::wstring name, std::wstring input) {
        return tests::Test{.name = name, .callback = [input] {
                             CHECK(!ParseSectionHeader(SingleLine(input)));
                           }};
      };
      auto ok = [](std::wstring name, std::wstring input,
                   std::wstring header_type = L"foo",
                   std::wstring value = L"bar") {
        return tests::Test{
            .name = name, .callback = [input, header_type, value] {
              auto output = ParseSectionHeader(SingleLine(input));
              CHECK(output) << "Unexpected error: " << output.error();
              CHECK_EQ(output->header_type, NonEmptySingleLine(header_type));
              CHECK_EQ(output->value, NonEmptySingleLine(value));
            }};
      };
      return {err(L"BadStart", L"foo bar"),
              err(L"MissingEnd", L"[foo bar"),
              err(L"TrailingGarbage", L"[foo bar]    heh   ]"),
              err(L"SingleToken", L"[foo]"),
              err(L"SingleTokenWithSpaces", L"  [  foo  ]  "),
              ok(L"Simple", L"[foo bar]"),
              ok(L"WithPrefixSpaces", L"   [foo bar]"),
              ok(L"WithSuffixSpaces", L"[foo bar]   "),
              ok(L"WithSpacesEverywhere", L"  [    foo  bar   ]   "),
              ok(L"WithStrangeTokens", L"[asdf:123 var/blah:12:32!]",
                 L"asdf:123", L"var/blah:12:32!")};
    }));
}  // namespace

struct KeyValue {
  NonEmptySingleLine key;
  NonEmptySingleLine value;
};
std::expected<KeyValue, Error> ParseKeyValue(NonEmptySingleLine input) {
  input = Trim(input, Space);
  std::optional<ColumnNumber> colon =
      FindFirstOf(input, {L':'}, ColumnNumber{1});
  if (!colon)
    return Error{ToLazyString(input) +
                 LazyString{L": Unable to parse: Expected `:`"}};
  auto key = NonEmptySingleLine::New(
      Trim(input.Substring(ColumnNumber{}, colon->ToDelta()), Space));
  auto value = NonEmptySingleLine::New(
      Trim(input.Substring(colon.value() + ColumnNumberDelta{1}), Space));
  if (!key) return Error{LazyString{L"Invalid line (no key): "} + input};
  if (!value)
    return Error{LazyString{L"Invalid line (value missing): "} + input};
  return KeyValue{.key = std::move(key).value(),
                  .value = std::move(value).value()};
}

const bool parse_key_value_tests_registration = tests::Register(
    L"LogModelParseKeyValue", std::invoke([] -> std::vector<tests::Test> {
      auto err = [](std::wstring name, std::wstring input) {
        return tests::Test{.name = name, .callback = [input] {
                             CHECK(!ParseKeyValue(SingleLine(input)));
                           }};
      };
      auto ok = [](std::wstring name, std::wstring input,
                   std::wstring key = L"foo", std::wstring value = L"bar") {
        return tests::Test{
            .name = name, .callback = [input, key, value] {
              auto output =
                  ParseKeyValue(NonEmptySingleLine(SingleLine(input)));
              CHECK(output) << "Unexpected error: " << output.error();
              CHECK_EQ(output->key, NonEmptySingleLine(key));
              CHECK_EQ(output->value, NonEmptySingleLine(value));
            }};
      };
      return {
          err(L"MissingColon", L"foo bar"),
          ok(L"Simple", L"foo:bar"),
          ok(L"WithSpacePrefix", L"   foo:bar"),
          ok(L"WithSpaceBeforeColon", L"foo   :bar"),
          ok(L"WithSpaceAfterColon", L"foo:   bar"),
          ok(L"WithSpacesEverywhere", L"    foo  :   bar  "),
          ok(L"WithMultipleColons", L"foo:   bar:quux:yeah", L"foo",
             L"bar:quux:yeah"),
      };
    }));

PossibleError DispatchLine(
    std::unordered_map<NonEmptySingleLine,
                       std::function<PossibleError(NonEmptySingleLine)>>
        handlers,
    SingleLine input) {
  input = Trim(input, {L' '});
  if (input.empty()) return EmptyValue{};
  DECLARE_OR_RETURN(KeyValue key_value,
                    ParseKeyValue(NonEmptySingleLine(input)));
  if (auto handler = handlers.find(key_value.key); handler != handlers.end())
    return handler->second(key_value.value);
  return Error{LazyString{L"Invalid directive: "} + key_value.key};
}

std::expected<LogView, Error> ParseLogView(const LineSequence& block) {
  if (block.size() < LineNumberDelta{2}) return Error{L"Short block found."};
  DECLARE_OR_RETURN(SectionHeader header,
                    ParseSectionHeader(block.at(LineNumber{})->contents()));
  CHECK_EQ(header.header_type, NON_EMPTY_SINGLE_LINE_CONSTANT(L"view"));
  LogViewName log_view_name{header.value};
  std::unordered_map<LogEntryName,
                     std::vector<language::lazy_string::NonEmptySingleLine>>
      expressions;
  std::vector<Error> errors =
      block | std::views::drop(1) |
      std::views::transform([&](Line line) -> PossibleError {
        return DispatchLine(
            {{NON_EMPTY_SINGLE_LINE_CONSTANT(L"variable"),
              [&expressions](NonEmptySingleLine value) -> PossibleError {
                std::optional<ColumnNumber> name_end =
                    FindFirstOf(value, Space);
                DECLARE_OR_RETURN(
                    Identifier name_identifier,
                    Identifier::New(NonEmptySingleLine::New(
                        value.Substring(ColumnNumber{}, name_end->ToDelta()))));
                std::optional<SingleLine> expr_str =
                    name_end.transform([&](ColumnNumber pos) {
                      return Trim(value.Substring(pos), Space);
                    });
                if (!expr_str) return Error{L"Expected: expression."};
                expressions[LogEntryName{name_identifier}].push_back(
                    expr_str.value());
                return EmptyValue{};
              }}},
            line.contents());
      }) |
      GetErrors | std::ranges::to<std::vector>();
  if (!errors.empty()) return MergeErrors(errors, L", ");
  LOG(INFO) << "Created view " << log_view_name
            << " with expressions: " << expressions.size();
  return LogView{.name = log_view_name, .expressions = std::move(expressions)};
}

std::expected<LogEntryValueType, Error> ParseValueType(SingleLine input) {
  input = LowerCase(input);
  if (input.empty()) return LogEntryValueType::String;
  if (input == SingleLine{L"path"}) return LogEntryValueType::Path;
  return Error{LazyString{L"Unknown value type: "} + input};
}

std::expected<LogType, Error> ParseLogType(const LineSequence& block) {
  if (block.size() < LineNumberDelta{2}) return Error{L"Short block found."};

  // Each block starts with the [type Name] line
  // TODO(P2, 2026-04-28): Remove comments.
  DECLARE_OR_RETURN(SectionHeader header,
                    ParseSectionHeader(block.at(LineNumber{})->contents()));
  CHECK_EQ(header.header_type, NON_EMPTY_SINGLE_LINE_CONSTANT(L"type"));
  LogTypeName log_type_name{header.value};
  LogTypeActivationPolicy activation_policy = LogTypeActivationPolicy::Explicit;
  std::vector<NonEmptySingleLine> patterns;
  std::vector<LogEntryConfiguration> entries;
  std::vector<Error> errors =
      block | std::views::drop(1) |
      std::views::transform([&](Line line) -> PossibleError {
        return DispatchLine(
            {{NON_EMPTY_SINGLE_LINE_CONSTANT(L"group"),
              [&entries](NonEmptySingleLine value) -> PossibleError {
                // Value looks like this: 0 src_path path
                // group_id entry_name [entry_type]
                std::optional<ColumnNumber> group_id_end =
                    FindFirstOf(value, Space);
                std::optional<ColumnNumber> entry_name_end =
                    group_id_end.transform([&value](ColumnNumber pos) {
                      return FindFirstOf(value, Space,
                                         pos + ColumnNumberDelta{1})
                          .value_or(ColumnNumber{} + value.size());
                    });
                std::optional<SingleLine> entry_name_str =
                    group_id_end.transform([&](ColumnNumber pos) {
                      return Trim(
                          value.Substring(pos, entry_name_end.value() - pos),
                          Space);
                    });
                if (!entry_name_str) return Error{L"Expected: entry name."};
                DECLARE_OR_RETURN(Identifier entry_name_identifier,
                                  Identifier::New(NonEmptySingleLine::New(
                                      entry_name_str.value())));
                DECLARE_OR_RETURN(
                    LogEntryValueType value_type,
                    ParseValueType(
                        Trim(value.Substring(entry_name_end.value()), Space)));
                return Visit(
                    AsInt(ToLazyString(value.Substring(
                        ColumnNumber{}, group_id_end->ToDelta()))),
                    [&](int group_id) -> PossibleError {
                      entries.push_back(LogEntryConfiguration{
                          .name = LogEntryName{entry_name_identifier},
                          .capturing_group = LogCapturingGroup{group_id},
                          .value_type = value_type});
                      return EmptyValue{};
                    },
                    [](Error error) -> PossibleError {
                      return AugmentError(L"Invalid group ID", error);
                    });
              }},
             {NON_EMPTY_SINGLE_LINE_CONSTANT(L"pattern"),
              [&patterns](NonEmptySingleLine value) {
                patterns.push_back(value);
                return EmptyValue{};
              }},
             {NON_EMPTY_SINGLE_LINE_CONSTANT(L"activation"),
              [&activation_policy](NonEmptySingleLine value) -> PossibleError {
                if (value == NON_EMPTY_SINGLE_LINE_CONSTANT(L"implicit"))
                  activation_policy = LogTypeActivationPolicy::Implicit;
                else if (value == NON_EMPTY_SINGLE_LINE_CONSTANT(L"explicit"))
                  activation_policy = LogTypeActivationPolicy::Explicit;
                else
                  return Error(LazyString{L"Invalid activation policy: "} +
                               value);
                return EmptyValue{};
              }}},
            line.contents());
      }) |
      GetErrors | std::ranges::to<std::vector>();
  std::wregex pattern;
  if (patterns.empty())
    errors.push_back(Error{L"No pattern specified."});
  else if (patterns.size() != 1)
    errors.push_back(Error{L"Multiple patterns specified."});
  else
    try {
      pattern = std::wregex(ToLazyString(patterns[0]).ToString());
    } catch (const std::regex_error& e) {
      errors.push_back(Error{LazyString{L"Invalid regex found: "} +
                             patterns[0] + LazyString{L": "} +
                             LazyString{FromByteString(e.what())}});
    }
  if (!errors.empty()) return MergeErrors(errors, L", ");
  return LogType(log_type_name, std::move(pattern), std::move(entries),
                 activation_policy);
}

std::expected<LogModel, language::Error> ParseLogConfig(
    const LineSequence& lines) {
  std::unordered_map<LogTypeName, LogType> log_types;
  std::unordered_map<LogViewName, LogView> views;

  std::vector<Error> errors =
      PartitionIntoBlocks(lines) |
      std::views::transform([&log_types,
                             &views](LineSequence block) -> PossibleError {
        DECLARE_OR_RETURN(
            SectionHeader header,
            ParseSectionHeader(block.at(LineNumber{})->contents()));
        if (header.header_type == NON_EMPTY_SINGLE_LINE_CONSTANT(L"type")) {
          DECLARE_OR_RETURN(LogType log_type, ParseLogType(block));
          log_types.insert({log_type.name(), log_type});
          return EmptyValue{};
        }
        if (header.header_type == NON_EMPTY_SINGLE_LINE_CONSTANT(L"view")) {
          DECLARE_OR_RETURN(LogView log_view, ParseLogView(block));
          views.insert({log_view.name, log_view});
          return EmptyValue{};
        }
        return Error{LazyString{L"Invalid directive: "} + header.header_type};
      }) |
      GetErrors | std::ranges::to<std::vector>();
  if (!errors.empty()) {
    Error error = MergeErrors(errors, L", ");
    LOG(INFO) << "Errors: " << error;
    return error;
  }
  LOG(INFO) << "Returning model";
  return LogModel{.log_types = std::move(log_types), .views = std::move(views)};
}

futures::ValueOrError<LogModel> LoadModelFromPaths(
    EditorState& editor, language::lazy_string::LazyString paths) {
  LOG(INFO) << "LoadModelFromPaths: " << paths;
  // TODO(P1, 2026-04-29, log): Break paths into comma-separated list of
  // paths.
  DECLARE_OR_RETURN(Path input_path, Path::New(paths));
  return UnwrapVectorFuture(
             editor.edge_path() |
             std::views::transform([&editor, input_path](Path edge_path)
                                       -> futures::ValueOrError<LogModel> {
               DECLARE_OR_RETURN(SingleLine path_pattern,
                                 SingleLine::New(ToLazyString(
                                     Path::Join(edge_path, input_path))));
               return OpenFiles(
                          OpenFilesOptions{
                              .editor = editor,
                              .not_found_handler =
                                  OpenFilesOptions::NotFoundHandler::Ignore,
                              .path_pattern = path_pattern,
                              .insertion_type =
                                  BuffersList::AddBufferType::Ignore,
                              .directory_filter =
                                  FilePredictorOptions::Filter::Exclude,
                              .special_file_filter =
                                  FilePredictorOptions::Filter::Exclude})
                   .Transform(
                       [](std::vector<gc::Root<OpenBuffer>> files)
                           -> futures::ValueOrError<gc::Root<OpenBuffer>> {
                         // TODO(P0, 2026-04-29, log): Don't just handle the
                         // first one! Don't ignore the rest.
                         if (files.empty()) return Error{L"No models found."};
                         return files[0]->WaitForEndOfFile();
                       })
                   .Transform([](gc::Root<OpenBuffer> model_root)
                                  -> futures::ValueOrError<LogModel> {
                     LineSequence snapshot = model_root->contents().snapshot();
                     LOG(INFO)
                         << "Parsing log model from " << model_root->name()
                         << ", lines: " << snapshot.size();
                     return model_root->editor().thread_pool().Run(
                         [snapshot] mutable {
                           return ParseLogConfig(std::move(snapshot));
                         });
                   });
             }) |
             std::ranges::to<std::vector>())
      .Transform([](std::vector<ValueOrError<LogModel>> model) {
        // TODO(P0, 2026-04-29, log): Don't just return the first.
        return model[0];
      });
}
}  // namespace afc::editor
