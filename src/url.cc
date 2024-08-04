#include "src/url.h"

#include <string>

#include "src/infrastructure/dirname.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/tests/tests.h"

using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;

namespace afc::editor {

/* static */
URL URL::FromPath(Path path) { return URL(L"file:" + path.read().ToString()); }

std::optional<URL::Schema> URL::schema() const {
  auto colon = read().find_first_of(L':');
  if (colon == std::wstring::npos) return std::nullopt;
  auto candidate = read().substr(0, colon);
  static const std::unordered_map<std::wstring, Schema> schemes = {
      {L"file", Schema::kFile},
      {L"http", Schema::kHttp},
      {L"https", Schema::kHttps}};
  auto it = schemes.find(candidate);
  return it == schemes.end() ? std::nullopt : std::make_optional(it->second);
}

namespace {
const bool schema_tests_registration = tests::Register(
    L"URL::Schema",
    {{.name = L"EmptyURL",
      .callback = [] { CHECK(!URL(L"").schema().has_value()); }},
     {.name = L"URLFromPath",
      .callback =
          [] {
            CHECK(
                URL::FromPath(ValueOrDie(Path::New(LazyString{L"foo/bar/hey"})))
                    .schema() == URL::Schema::kFile);
          }},
     {.name = L"URLRelative",
      .callback = [] { CHECK(!URL(L"foo/bar/hey").schema().has_value()); }},
     {.name = L"URLStringFile", .callback = [] {
        CHECK(URL(L"file:foo/bar/hey").schema() == URL::Schema::kFile);
      }}});
}  // namespace

ValueOrError<Path> URL::GetLocalFilePath() const {
  std::optional<Schema> s = schema();
  // TODO(trivial, 2024-08-04): Get rid of LazyString.
  if (!s.has_value()) return Path::New(LazyString{read()});
  if (s != Schema::kFile) return Error(L"Schema isn't file.");
  // TODO(trivial, 2024-08-04): Get rid of LazyString.
  return Path::New(LazyString{read().substr(sizeof("file:") - 1)});
}

namespace {
const bool get_local_file_path_tests_registration = tests::Register(
    L"URL::GetLocalFilePath",
    {{.name = L"EmptyURL",
      .callback =
          [] {
            CHECK(std::holds_alternative<Error>(URL(L"").GetLocalFilePath()));
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
            CHECK(ValueOrDie(URL(input.read().ToString()).GetLocalFilePath()) ==
                  input);
          }},
     {.name = L"URLStringFile", .callback = [] {
        LazyString input{L"foo/bar/hey"};
        CHECK(ValueOrDie(URL(L"file:" + input.ToString()).GetLocalFilePath()) ==
              ValueOrDie(Path::New(input)));
      }}});
}  // namespace

LazyString URL::ToString() const { return LazyString{read()}; }

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
