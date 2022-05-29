#include "src/url.h"

#include <string>

#include "src/infrastructure/dirname.h"
#include "src/tests/tests.h"

namespace afc::editor {
using infrastructure::Path;
using language::Error;
using language::ValueOrError;

/* static */
URL URL::FromPath(Path path) { return URL(L"file:" + path.read()); }

std::optional<URL::Schema> URL::schema() const {
  auto colon = value_.find_first_of(L':');
  if (colon == std::wstring::npos) return std::nullopt;
  auto candidate = value_.substr(0, colon);
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
            CHECK(URL::FromPath(ValueOrDie(Path::FromString(L"foo/bar/hey")))
                      .schema() == URL::Schema::kFile);
          }},
     {.name = L"URLRelative",
      .callback = [] { CHECK(!URL(L"foo/bar/hey").schema().has_value()); }},
     {.name = L"URLStringFile", .callback = [] {
        CHECK(URL(L"file:foo/bar/hey").schema() == URL::Schema::kFile);
      }}});
}

ValueOrError<Path> URL::GetLocalFilePath() const {
  std::optional<Schema> s = schema();
  if (!s.has_value()) return Path::FromString(value_);
  if (s != Schema::kFile) return Error(L"Schema isn't file.");
  return Path::FromString(value_.substr(sizeof("file:") - 1));
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
            Path input = ValueOrDie(Path::FromString(L"foo/bar/hey"));
            CHECK(ValueOrDie(URL::FromPath(input).GetLocalFilePath()) == input);
          }},
     {.name = L"URLRelative",
      .callback =
          [] {
            Path input = ValueOrDie(Path::FromString(L"foo/bar/hey"));
            CHECK(ValueOrDie(URL(input.read()).GetLocalFilePath()) == input);
          }},
     {.name = L"URLStringFile", .callback = [] {
        std::wstring input = L"foo/bar/hey";
        CHECK(ValueOrDie(URL(L"file:" + input).GetLocalFilePath()) ==
              ValueOrDie(Path::FromString(input)));
      }}});
}

std::wstring URL::ToString() const { return value_; }
}  // namespace afc::editor
