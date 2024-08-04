#include "src/infrastructure/dirname.h"

#include <cstring>

extern "C" {
#include <libgen.h>
}

#include <glog/logging.h>

#include "src/language/container.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::Error;
using afc::language::FromByteString;
using afc::language::NewError;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::FindLastOf;
using afc::language::lazy_string::LazyString;

namespace afc::infrastructure {

using ::operator<<;

/* static */ language::PossibleError PathComponentValidator::Validate(
    const LazyString& input) {
  if (input.IsEmpty())
    return NewError(LazyString{L"Component can't be empty."});
  if (FindFirstOf(input, {L'/'}).has_value())
    return NewError(LazyString{L"Component can't contain a slash: "} +
                    LazyString{input});
  return Success();
}

namespace {
const bool path_component_constructor_good_inputs_tests_registration =
    tests::Register(
        L"PathComponentConstructorGoodInputs",
        {
            {.name = L"Simple",
             .callback = [] { PathComponent{LazyString{L"foo"}}; }},
            {.name = L"WithExtension",
             .callback = [] { PathComponent{LazyString{L"foo.md"}}; }},
        });

const bool path_component_constructor_bad_inputs_tests_registration =
    tests::Register(
        L"PathComponentConstructorBadInputs",
        {
            {.name = L"Empty",
             .callback =
                 [] { CHECK(IsError(PathComponent::New(LazyString{}))); }},
            {.name = L"EmptyCrash",
             .callback =
                 [] {
                   tests::ForkAndWaitForFailure(
                       [] { PathComponent{LazyString{}}; });
                 }},
            {.name = L"TooLarge",
             .callback =
                 [] {
                   CHECK(IsError(PathComponent::New(LazyString{L"foo/bar"})));
                 }},
            {.name = L"TooLargeCrash",
             .callback =
                 [] {
                   tests::ForkAndWaitForFailure(
                       [] { PathComponent{LazyString{L"foo/bar"}}; });
                 }},

        });
}  // namespace

/*  static */ PathComponent PathComponent::WithExtension(
    const PathComponent& path, const LazyString& extension) {
  return PathComponent(
      VisitOptional(
          [&path](ColumnNumber index) {
            return path.read().Substring(ColumnNumber{}, index.ToDelta());
          },
          [&path] { return path.read(); }, FindLastOf(path.read(), {L'.'})) +
      LazyString{L"."} + extension);
}

const bool path_component_with_extension_tests_registration = tests::Register(
    L"PathComponentWithExtension",
    {{.name = L"Absent",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo"), LazyString{L"md"}),
                     PathComponent::FromString(L"foo.md"));
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo"), LazyString{}),
                     PathComponent::FromString(L"foo."));
          }},
     {.name = L"Present",
      .callback =
          [] {
            CHECK_EQ(
                PathComponent::WithExtension(
                    PathComponent::FromString(L"foo.txt"), LazyString{L"md"}),
                PathComponent::FromString(L"foo.md"));
          }},
     {.name = L"MultipleReplacesOnlyLast", .callback = [] {
        CHECK_EQ(
            PathComponent::WithExtension(
                PathComponent::FromString(L"foo.blah.txt"), LazyString{L"md"}),
            PathComponent::FromString(L"foo.blah.md"));
      }}});

ValueOrError<PathComponent> PathComponent::remove_extension() const {
  return VisitOptional(
      [this](ColumnNumber index) {
        return PathComponent::New(
            read().Substring(ColumnNumber{}, index.ToDelta()));
      },
      [this] { return Success(*this); }, FindLastOf(read(), {L'.'}));
}

const bool path_component_remove_extension_tests_registration = tests::Register(
    L"PathComponentRemoveExtension",
    {{.name = L"Absent",
      .callback =
          [] {
            CHECK(
                ValueOrDie(PathComponent::FromString(L"foo").remove_extension(),
                           L"tests") == PathComponent::FromString(L"foo"));
          }},
     {.name = L"hidden",
      .callback =
          [] {
            CHECK(std::holds_alternative<Error>(
                PathComponent::FromString(L".blah").remove_extension()));
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(ValueOrDie(
                      PathComponent::FromString(L"foo.").remove_extension(),
                      L"tests") == PathComponent::FromString(L"foo"));
          }},
     {.name = L"Present", .callback = [] {
        CHECK(
            ValueOrDie(PathComponent::FromString(L"foo.md").remove_extension(),
                       L"tests") == PathComponent::FromString(L"foo"));
      }}});

std::optional<LazyString> PathComponent::extension() const {
  return VisitOptional(
      [this](ColumnNumber index) -> std::optional<LazyString> {
        return read().Substring(index + ColumnNumberDelta{1});
      },
      [] { return std::nullopt; }, FindLastOf(read(), {L'.'}));
}

const bool path_component_extension_tests_registration = tests::Register(
    L"PathComponentExtension",
    {{.name = L"Absent",
      .callback =
          [] {
            CHECK(!PathComponent::FromString(L"foo").extension().has_value());
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(PathComponent::FromString(L"foo.")
                      .extension()
                      .value()
                      .IsEmpty());
          }},
     {.name = L"Present", .callback = [] {
        CHECK(PathComponent::FromString(L"foo.md").extension().value() ==
              LazyString{L"md"});
      }}});

Path::Path(PathComponent path_component) : Path(path_component.read()) {}

/* static */ Path Path::Join(Path a, Path b) {
  if (a.IsRoot() && b.IsRoot()) {
    return b;
  }
  if (a == LocalDirectory() && b.read().get(ColumnNumber{}) != L'/') {
    return b;
  }
  bool has_slash = a.read().get(ColumnNumber{} + a.read().size() -
                                ColumnNumberDelta(1)) == L'/' ||
                   b.read().get(ColumnNumber{}) == L'/';
  return ValueOrDie(
      Path::New(a.read() + (has_slash ? LazyString{L""} : LazyString{L"/"}) +
                b.read()),
      L"Path::Join");
}

const bool path_join_tests_registration = tests::Register(
    L"PathJoinTests",
    {{.name = L"LocalRedundant",
      .callback =
          [] {
            CHECK_EQ(Path::Join(Path::LocalDirectory(),
                                ValueOrDie(Path::New(LazyString{L"alejo.txt"}),
                                           L"tests")),
                     ValueOrDie(Path::New(LazyString{L"alejo.txt"}), L"tests"));
          }},
     {.name = L"LocalImportant", .callback = [] {
        CHECK_EQ(Path::Join(Path::LocalDirectory(),
                            ValueOrDie(Path::New(LazyString{L"/alejo.txt"}),
                                       L"tests")),
                 ValueOrDie(Path::New(LazyString{L"./alejo.txt"}), L"tests"));
      }}});

Path Path::ExpandHomeDirectory(const Path& home_directory, const Path& path) {
  // TODO: Also support ~user/foo.
  return std::visit(
      overload{[&](Error) { return path; },
               [&](std::list<PathComponent> components) {
                 if (components.empty() ||
                     components.front() != PathComponent::FromString(L"~"))
                   return path;
                 components.pop_front();
                 return container::Fold(
                     [](PathComponent c, Path output) {
                       return Path::Join(std::move(output), std::move(c));
                     },
                     home_directory, std::move(components));
               }},
      path.DirectorySplit());
}

const bool expand_home_directory_tests_registration = tests::Register(
    L"ExpandHomeDirectoryTests",
    {
        {.name = L"NoExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"foo/bar"}), L"tests")),
                   ValueOrDie(Path::New(LazyString{L"foo/bar"}), L"tests"));
             }},
        {.name = L"MinimalExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"~"}), L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/home/alejo"}), L"tests"));
             }},
        {.name = L"SmallExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"~/"}), L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/home/alejo"}), L"tests"));
             }},
        {.name = L"LongExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"~/foo/bar"}),
                                  L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/home/alejo/foo/bar"}),
                              L"tests"));
             }},
        {.name = L"LongExpansionRedundantSlash",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo/"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"~/foo/bar"}),
                                  L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/home/alejo/foo/bar"}),
                              L"tests"));
             }},
    });

/* static */ Path Path::WithExtension(const Path& path,
                                      const LazyString& extension) {
  return Path::Join(
      ValueOrDie(path.Dirname(), L"Path::WithExtension"),
      PathComponent::WithExtension(
          ValueOrDie(path.Basename(), L"Path::WithExtension"), extension));
}

ValueOrError<Path> Path::Dirname() const {
  VLOG(5) << "Dirname: " << read();
  std::unique_ptr<char, decltype(&std::free)> tmp(
      strdup(ToByteString().c_str()), &std::free);
  CHECK(tmp != nullptr);
  return Path::New(LazyString{FromByteString(dirname(tmp.get()))});
}

ValueOrError<PathComponent> Path::Basename() const {
  VLOG(5) << "Pathname: " << read();
  std::unique_ptr<char, decltype(&std::free)> tmp(
      strdup(ToByteString().c_str()), &std::free);
  CHECK(tmp != nullptr);
  return PathComponent::New(LazyString{FromByteString(basename(tmp.get()))});
}

std::optional<LazyString> Path::extension() const {
  return std::visit(
      overload{[](Error) { return std::optional<LazyString>(); },
               [](PathComponent component) {
                 return std::optional<LazyString>(component.extension());
               }},
      Basename());
}

ValueOrError<std::list<PathComponent>> Path::DirectorySplit() const {
  std::list<PathComponent> output;
  Path path = *this;
  while (!path.IsRoot() && path != Path::LocalDirectory()) {
    ASSIGN_OR_RETURN(PathComponent base, path.Basename());
    using ::operator<<;
    VLOG(5) << "DirectorySplit: PushFront: " << base;
    output.push_front(base);
    if (output.front().read() == path.read()) return Success(output);
    ASSIGN_OR_RETURN(
        auto dir, AugmentError(LazyString{L"Dirname error"}, path.Dirname()));
    if (dir.read().size() >= path.read().size()) {
      LOG(INFO) << "Unable to advance: " << path << " -> " << dir;
      return NewError(LazyString{L"Unable to advance: "} + path.read());
    }
    VLOG(5) << "DirectorySplit: Advance: " << dir;
    path = std::move(dir);
  }
  return Success(output);
}

const bool directory_split_tests_registration = tests::Register(
    L"DirectorySplitTests",
    {
        {.name = L"NoSplit",
         .callback =
             [] {
               std::list<PathComponent> result = ValueOrDie(
                   ValueOrDie(Path::New(LazyString{L"alejo.txt"}), L"tests")
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(result.front(),
                        PathComponent::FromString(L"alejo.txt"));
             }},
        {.name = L"Directory",
         .callback =
             [] {
               auto result = ValueOrDie(
                   ValueOrDie(Path::New(LazyString{L"alejo/"}), L"tests").DirectorySplit(),
                   L"tests");
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(result.front(), PathComponent::FromString(L"alejo"));
             }},
        {.name = L"LongSplit",
         .callback =
             [] {
               auto result_list = ValueOrDie(
                   Path{ValueOrDie(Path::New(LazyString{L"aaa/b/cc/ddd"}), L"tests")}
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result_list.size(), 4ul);
               std::vector<PathComponent> result(result_list.begin(),
                                                 result_list.end());
               CHECK_EQ(result[0], PathComponent::FromString(L"aaa"));
               CHECK_EQ(result[1], PathComponent::FromString(L"b"));
               CHECK_EQ(result[2], PathComponent::FromString(L"cc"));
               CHECK_EQ(result[3], PathComponent::FromString(L"ddd"));
             }},
        {.name = L"LongSplitMultiSlash",
         .callback =
             [] {
               auto result_list = ValueOrDie(
                   ValueOrDie(Path::New(LazyString{L"aaa////b////cc/////ddd"}), L"tests")
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result_list.size(), 4ul);
               std::vector<PathComponent> result(result_list.begin(),
                                                 result_list.end());
               CHECK_EQ(result[0], PathComponent::FromString(L"aaa"));
               CHECK_EQ(result[1], PathComponent::FromString(L"b"));
               CHECK_EQ(result[2], PathComponent::FromString(L"cc"));
               CHECK_EQ(result[3], PathComponent::FromString(L"ddd"));
             }},
    });

bool Path::IsRoot() const { return read() == LazyString{L"/"}; }

Path::RootType Path::GetRootType() const {
  return read().get(ColumnNumber{}) == L'/' ? Path::RootType::kAbsolute
                           : Path::RootType::kRelative;
}

ValueOrError<AbsolutePath> Path::Resolve() const {
  char* result = realpath(ToByteString().c_str(), nullptr);
  return result == nullptr ? Error(FromByteString(strerror(errno)))
                           : AbsolutePath::FromString(LazyString{FromByteString(result)});
}

PossibleError PathValidator::Validate(const LazyString& path) {
  if (path.IsEmpty()) return NewError(LazyString{L"Path can not be empty."});
  return Success();
}

Path Path::LocalDirectory() {
  return ValueOrDie(Path::New(LazyString{L"."}), L"Path::LocalDirectory");
}
Path Path::Root() { return ValueOrDie(Path::New(LazyString{L"/"}), L"Path::Root"); }

std::string Path::ToByteString() const {
  return afc::language::ToByteString(read().ToString());
}

ValueOrError<AbsolutePath> AbsolutePath::FromString(LazyString path) {
  if (path.IsEmpty()) {
    return Error(L"Path can't be empty");
  }
  if (path.get(ColumnNumber{}) != L'/') {
    return Error(L"Absolute path must start with /");
  }
  return Success(AbsolutePath(std::move(path)));
}

AbsolutePath::AbsolutePath(LazyString path) : Path(std::move(path)) {}

std::ostream& operator<<(std::ostream& os, const Path& p) {
  os << p.read();
  return os;
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  return Path::Join(ValueOrDie(Path::New(LazyString{a}), L"PathJoin"),
                    ValueOrDie(Path::New(LazyString{b}), L"PathJoin"))
      .read().ToString();
}

std::unique_ptr<DIR, std::function<void(DIR*)>> OpenDir(std::wstring path) {
  VLOG(10) << "Open dir: " << path;
  return std::unique_ptr<DIR, std::function<void(DIR*)>>(
      opendir(ToByteString(path).c_str()), closedir);
}

}  // namespace afc::infrastructure
