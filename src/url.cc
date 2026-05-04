#include "src/url.h"

#include <string>

#include "src/infrastructure/dirname.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"

using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::GetValueOrDefault;
using afc::language::IgnoreErrors;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::lazy_string::ToLazyString;

namespace afc::editor {

/* static */
URL URL::FromPath(Path path) {
  return URL{NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"file:")} +
             vm::EscapedString(path.read()).URLRepresentation()};
}

std::optional<URL::Scheme> URL::scheme() const {
  std::optional<ColumnNumber> colon = FindFirstOf(read(), {L':'});
  if (colon == std::nullopt) return std::nullopt;
  SingleLine candidate = read().Substring(ColumnNumber{}, colon->ToDelta());
  static const std::unordered_map<SingleLine, std::optional<Scheme>> schemes = {
      {SINGLE_LINE_CONSTANT(L"file"), Scheme::File},
      {SINGLE_LINE_CONSTANT(L"http"), Scheme::Http},
      {SINGLE_LINE_CONSTANT(L"https"), Scheme::Https},
      {SINGLE_LINE_CONSTANT(L"vm"), Scheme::Vm}};
  return GetValueOrDefault(schemes, candidate, std::optional<Scheme>{});
}

namespace {
const bool scheme_tests_registration = tests::Register(
    L"URL::Scheme",
    {{.name = L"URLFromPath",
      .callback =
          [] {
            CHECK(
                URL::FromPath(ValueOrDie(Path::New(LazyString{L"foo/bar/hey"})))
                    .scheme() == URL::Scheme::File);
          }},
     {.name = L"URLFromPathWithNewlineAndSpace",
      .callback =
          [] {
            const Path path{LazyString{L"fo o/bar\nhey"}};
            URL url = URL::FromPath(path);
            CHECK(url.scheme() == URL::Scheme::File);
            CHECK_EQ(ValueOrDie(url.GetLocalFilePath()), path);
          }},
     {.name = L"URLRelative",
      .callback =
          [] {
            CHECK(!URL(NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"foo/bar/hey")})
                       .scheme()
                       .has_value());
          }},
     {.name = L"URLVm",
      .callback =
          [] {
            CHECK(URL(NonEmptySingleLine(
                          SINGLE_LINE_CONSTANT(L"vm:buffer.Close();")))
                      .scheme() == URL::Scheme::Vm);
          }},
     {.name = L"URLStringFile", .callback = [] {
        CHECK(URL(NonEmptySingleLine(SINGLE_LINE_CONSTANT(L"file:foo/bar/hey")))
                  .scheme() == URL::Scheme::File);
      }}});
}  // namespace

ValueOrError<Path> URL::GetLocalFilePath() const {
  std::optional<Scheme> s = scheme();
  if (!s.has_value()) {
    DECLARE_OR_RETURN(vm::EscapedString escaped_string,
                      vm::EscapedString::ParseURL(read().read()));
    return Path::New(escaped_string.OriginalString());
  }
  if (s != Scheme::File) return Error{LazyString{L"Scheme isn't file."}};
  ASSIGN_OR_RETURN(vm::EscapedString url_input,
                   vm::EscapedString::ParseURL(
                       read().Substring(ColumnNumber{sizeof("file:") - 1})));
  return Path::New(url_input.OriginalString());
}

namespace {
const bool get_local_file_path_tests_registration = tests::Register(
    L"URL::GetLocalFilePath",
    {{.name = L"URLFromPath",
      .callback =
          [] {
            Path input = ValueOrDie(Path::New(LazyString{L"foo/bar/hey"}));
            CHECK_EQ(ValueOrDie(URL::FromPath(input).GetLocalFilePath()),
                     input);
          }},
     {.name = L"URLRelative",
      .callback =
          [] {
            const NonEmptySingleLine input{
                SINGLE_LINE_CONSTANT(L"foo/bar/hey")};
            CHECK_EQ(ValueOrDie(URL{input}.GetLocalFilePath()),
                     ValueOrDie(Path::New(ToLazyString(input))));
          }},
     {.name = L"URLStringFile", .callback = [] {
        const NonEmptySingleLine input{SINGLE_LINE_CONSTANT(L"foo/bar/hey")};
        CHECK_EQ(
            ValueOrDie(
                URL(SINGLE_LINE_CONSTANT(L"file:") + input).GetLocalFilePath()),
            ValueOrDie(Path::New(ToLazyString(input))));
      }}});
}  // namespace

SingleLine URL::StripScheme() const {
  NonEmptySingleLine str = read();
  return vm::EscapedString::ParseURL(FindFirstOf(str, {L':'})
                                         .transform([&](ColumnNumber colon) {
                                           return str.Substring(
                                               colon + ColumnNumberDelta{1});
                                         })
                                         .value_or(str))
      .value()
      .OriginalString();
}

namespace {
const bool strip_scheme_tests_registration = tests::Register(
    L"URL::StripScheme",
    {
        {.name = L"NoScheme",
         .callback =
             [] {
               Path input = ValueOrDie(Path::New(L"foo/bar/hey"));
               CHECK_EQ(URL::FromPath(input).StripScheme(),
                        ToLazyString(input));
             }},
        {.name = L"Https",
         .callback =
             [] {
               CHECK_EQ(
                   URL(NON_EMPTY_SINGLE_LINE_CONSTANT(L"http://foo/bar?yeah"))
                       .StripScheme(),
                   SINGLE_LINE_CONSTANT(L"//foo/bar?yeah"));
             }},
        {.name = L"Vm",
         .callback =
             [] {
               CHECK_EQ(
                   URL(NON_EMPTY_SINGLE_LINE_CONSTANT(L"vm:buffer.Close();"))
                       .StripScheme(),
                   SINGLE_LINE_CONSTANT(L"buffer.Close();"));
             }},
    });
}  // namespace

std::vector<URL> GetLocalFileURLsWithExtensions(
    const SingleLine& file_context_extensions, const URL& url) {
  std::vector<URL> output = {url};
  VisitValue(url.GetLocalFilePath(), [&](Path path) {
    std::ranges::copy(
        TokenizeBySpaces(file_context_extensions) |
            std::views::transform([&path](const Token& extension) {
              return URL::FromPath(
                  Path::WithExtension(path, ToLazyString(extension.value)));
            }),
        std::back_inserter(output));
  });
  return output;
}
}  // namespace afc::editor
