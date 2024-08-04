#include "src/url.h"

#include <string>

#include "src/infrastructure/dirname.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/tests/tests.h"

using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::GetValueOrDefault;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;

namespace afc::editor {

/* static */
URL URL::FromPath(Path path) { return URL{LazyString{L"file:"} + path.read()}; }

std::optional<URL::Schema> URL::schema() const {
  std::optional<ColumnNumber> colon = FindFirstOf(read(), {L':'});
  if (colon == std::nullopt) return std::nullopt;
  LazyString candidate = read().Substring(ColumnNumber{}, colon->ToDelta());
  static const std::unordered_map<LazyString, std::optional<Schema>> schemes = {
      {LazyString{L"file"}, Schema::kFile},
      {LazyString{L"http"}, Schema::kHttp},
      {LazyString{L"https"}, Schema::kHttps}};
  return GetValueOrDefault(schemes, candidate, std::optional<Schema>{});
}

namespace {
const bool schema_tests_registration = tests::Register(
    L"URL::Schema",
    {{.name = L"EmptyURL",
      .callback = [] { CHECK(!URL(LazyString{}).schema().has_value()); }},
     {.name = L"URLFromPath",
      .callback =
          [] {
            CHECK(
                URL::FromPath(ValueOrDie(Path::New(LazyString{L"foo/bar/hey"})))
                    .schema() == URL::Schema::kFile);
          }},
     {.name = L"URLRelative",
      .callback =
          [] { CHECK(!URL(LazyString{L"foo/bar/hey"}).schema().has_value()); }},
     {.name = L"URLStringFile", .callback = [] {
        CHECK(URL(LazyString{L"file:foo/bar/hey"}).schema() ==
              URL::Schema::kFile);
      }}});
}  // namespace

ValueOrError<Path> URL::GetLocalFilePath() const {
  std::optional<Schema> s = schema();
  if (!s.has_value()) return Path::New(read());
  if (s != Schema::kFile) return Error(L"Schema isn't file.");
  return Path::New(read().Substring(ColumnNumber{sizeof("file:") - 1}));
}

namespace {
const bool get_local_file_path_tests_registration = tests::Register(
    L"URL::GetLocalFilePath",
    {{.name = L"EmptyURL",
      .callback =
          [] {
            CHECK(std::holds_alternative<Error>(
                URL(LazyString{}).GetLocalFilePath()));
          }},
     {.name = L"URLFromPath",
      .callback =
          [] {
            Path input = ValueOrDie(Path::New(LazyString{L"foo/bar/hey"}));
            CHECK(ValueOrDie(URL::FromPath(input).GetLocalFilePath()) == input);
          }},
     {.name = L"URLRelative",
      .callback =
          [] {
            Path input = ValueOrDie(Path::New(LazyString{L"foo/bar/hey"}));
            CHECK(ValueOrDie(URL(input.read()).GetLocalFilePath()) == input);
          }},
     {.name = L"URLStringFile", .callback = [] {
        LazyString input{L"foo/bar/hey"};
        CHECK(
            ValueOrDie(URL(LazyString{L"file:"} + input).GetLocalFilePath()) ==
            ValueOrDie(Path::New(input)));
      }}});
}  // namespace

std::vector<URL> GetLocalFileURLsWithExtensions(
    const LazyString& file_context_extensions, const URL& url) {
  std::vector<URL> output = {url};
  return std::visit(
      overload{[&](Error) { return output; },
               [&](Path path) {
                 std::vector<Token> extensions =
                     TokenizeBySpaces(file_context_extensions);
                 for (const Token& extension_token : extensions) {
                   CHECK(!extension_token.value.IsEmpty());
                   output.push_back(URL::FromPath(
                       Path::WithExtension(path, extension_token.value)));
                 }
                 return output;
               }},
      url.GetLocalFilePath());
}
}  // namespace afc::editor
