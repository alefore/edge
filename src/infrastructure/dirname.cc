#include "src/infrastructure/dirname.h"

#include <cstring>

extern "C" {
#include <libgen.h>
}

#include <glog/logging.h>

#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::Error;
using afc::language::FromByteString;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::infrastructure {
ValueOrError<PathComponent> PathComponent::FromString(std::wstring component) {
  if (component.empty()) {
    return Error(L"Component can't be empty.");
  }
  if (component.find(L"/") != std::wstring::npos) {
    return Error(L"Component can't contain a slash: " + component);
  }
  return Success(PathComponent(std::move(component)));
}

/*  static */ PathComponent PathComponent::WithExtension(
    const PathComponent& path, const std::wstring& extension) {
  auto index = path.component_.find_last_of(L".");
  return PathComponent((index == std::string::npos
                            ? path.component_
                            : path.component_.substr(0, index)) +
                       L"." + extension);
}

const bool path_component_with_extension_tests_registration = tests::Register(
    L"PathComponentWithExtension",
    {{.name = L"Absent",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo"), L"md"),
                     PathComponent::FromString(L"foo.md"));
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo"), L""),
                     PathComponent::FromString(L"foo."));
          }},
     {.name = L"Present",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo.txt"), L"md"),
                     PathComponent::FromString(L"foo.md"));
          }},
     {.name = L"MultipleReplacesOnlyLast", .callback = [] {
        CHECK_EQ(PathComponent::WithExtension(
                     PathComponent::FromString(L"foo.blah.txt"), L"md"),
                 PathComponent::FromString(L"foo.blah.md"));
      }}});

const std::wstring& PathComponent::ToString() const { return component_; }
size_t PathComponent::size() const { return component_.size(); }

ValueOrError<PathComponent> PathComponent::remove_extension() const {
  auto index = component_.find_last_of(L".");
  if (index == std::string::npos) {
    return Success(*this);
  }
  return PathComponent::FromString(component_.substr(0, index));
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

std::optional<std::wstring> PathComponent::extension() const {
  auto index = component_.find_last_of(L".");
  if (index == std::string::npos) {
    return std::nullopt;
  }
  return component_.substr(index + 1);
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
            CHECK(
                PathComponent::FromString(L"foo.").extension().value().empty());
          }},
     {.name = L"Present", .callback = [] {
        CHECK(PathComponent::FromString(L"foo.md").extension().value() ==
              L"md");
      }}});

PathComponent::PathComponent(std::wstring component)
    : component_(std::move(component)) {}

Path::Path(PathComponent path_component)
    : path_(std::move(path_component.component_)) {}

/* static */ Path Path::Join(Path a, Path b) {
  if (a.IsRoot() && b.IsRoot()) {
    return b;
  }
  if (a == LocalDirectory() && b.path_[0] != L'/') {
    return b;
  }
  bool has_slash = a.path_[a.path_.size() - 1] == L'/' || b.path_[0] == L'/';
  return ValueOrDie(
      Path::FromString(a.path_ + (has_slash ? L"" : L"/") + b.path_),
      L"Path::Join");
}

const bool path_join_tests_registration = tests::Register(
    L"PathJoinTests",
    {{.name = L"LocalRedundant",
      .callback =
          [] {
            CHECK_EQ(Path::Join(
                         Path::LocalDirectory(),
                         ValueOrDie(Path::FromString(L"alejo.txt"), L"tests")),
                     ValueOrDie(Path::FromString(L"alejo.txt"), L"tests"));
          }},
     {.name = L"LocalImportant", .callback = [] {
        CHECK_EQ(
            Path::Join(Path::LocalDirectory(),
                       ValueOrDie(Path::FromString(L"/alejo.txt"), L"tests")),
            ValueOrDie(Path::FromString(L"./alejo.txt"), L"tests"));
      }}});

ValueOrError<Path> Path::FromString(std::wstring path) {
  return Path::FromString(LazyString{std::move(path)});
}

ValueOrError<Path> Path::FromString(LazyString path) {
  // TODO(easy, 2022-06-10): Avoid call to ToString.
  return path.size().IsZero() ? Error(L"Empty path.")
                              : Success(Path(path.ToString()));
}

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
                       ValueOrDie(Path::FromString(L"/home/alejo"), L"tests"),
                       ValueOrDie(Path::FromString(L"foo/bar"), L"tests")),
                   ValueOrDie(Path::FromString(L"foo/bar"), L"tests"));
             }},
        {.name = L"MinimalExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::FromString(L"/home/alejo"), L"tests"),
                       ValueOrDie(Path::FromString(L"~"), L"tests")),
                   ValueOrDie(Path::FromString(L"/home/alejo"), L"tests"));
             }},
        {.name = L"SmallExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::FromString(L"/home/alejo"), L"tests"),
                       ValueOrDie(Path::FromString(L"~/"), L"tests")),
                   ValueOrDie(Path::FromString(L"/home/alejo"), L"tests"));
             }},
        {.name = L"LongExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::FromString(L"/home/alejo"), L"tests"),
                       ValueOrDie(Path::FromString(L"~/foo/bar"), L"tests")),
                   ValueOrDie(Path::FromString(L"/home/alejo/foo/bar"),
                              L"tests"));
             }},
        {.name = L"LongExpansionRedundantSlash",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::FromString(L"/home/alejo/"), L"tests"),
                       ValueOrDie(Path::FromString(L"~/foo/bar"), L"tests")),
                   ValueOrDie(Path::FromString(L"/home/alejo/foo/bar"),
                              L"tests"));
             }},
    });

/* static */ Path Path::WithExtension(const Path& path,
                                      const std::wstring& extension) {
  return Path::Join(
      ValueOrDie(path.Dirname(), L"Path::WithExtension"),
      PathComponent::WithExtension(
          ValueOrDie(path.Basename(), L"Path::WithExtension"), extension));
}

ValueOrError<Path> Path::Dirname() const {
  VLOG(5) << "Dirname: " << path_;
  std::unique_ptr<char, decltype(&std::free)> tmp(
      strdup(ToByteString(path_).c_str()), &std::free);
  CHECK(tmp != nullptr);
  return Path::FromString(FromByteString(dirname(tmp.get())));
}

ValueOrError<PathComponent> Path::Basename() const {
  VLOG(5) << "Pathname: " << path_;
  std::unique_ptr<char, decltype(&std::free)> tmp(
      strdup(ToByteString(path_).c_str()), &std::free);
  CHECK(tmp != nullptr);
  return PathComponent::FromString(FromByteString(basename(tmp.get())));
}

std::optional<std::wstring> Path::extension() const {
  return std::visit(
      overload{[](Error) { return std::optional<std::wstring>(); },
               [](PathComponent component) {
                 return std::optional<std::wstring>(component.extension());
               }},
      Basename());
}

const std::wstring& Path::read() const { return path_; }

ValueOrError<std::list<PathComponent>> Path::DirectorySplit() const {
  std::list<PathComponent> output;
  Path path = *this;
  while (!path.IsRoot() && path != Path::LocalDirectory()) {
    ASSIGN_OR_RETURN(auto base, path.Basename());
    VLOG(5) << "DirectorySplit: PushFront: " << base;
    output.push_front(base);
    if (output.front().ToString() == path.path_) {
      return Success(output);
    }
    ASSIGN_OR_RETURN(
        auto dir, AugmentError(LazyString{L"Dirname error"}, path.Dirname()));
    if (dir.path_.size() >= path.path_.size()) {
      LOG(INFO) << "Unable to advance: " << path << " -> " << dir;
      return Error(L"Unable to advance: " + path.read());
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
               std::list<PathComponent> result =
                   ValueOrDie(Path{PathComponent::FromString(L"alejo.txt")}
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
                   Path{PathComponent::FromString(L"alejo/")}.DirectorySplit(),
                   L"tests");
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(result.front(), PathComponent::FromString(L"alejo"));
             }},
        {.name = L"LongSplit",
         .callback =
             [] {
               auto result_list =
                   ValueOrDie(Path{PathComponent::FromString(L"aaa/b/cc/ddd")}
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
                   ValueOrDie(Path::FromString(L"aaa////b////cc/////ddd"),
                              L"tests")
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

bool Path::IsRoot() const { return path_ == L"/"; }

Path::RootType Path::GetRootType() const {
  return path_[0] == L'/' ? Path::RootType::kAbsolute
                          : Path::RootType::kRelative;
}

ValueOrError<AbsolutePath> Path::Resolve() const {
  char* result = realpath(ToByteString(path_).c_str(), nullptr);
  return result == nullptr ? Error(FromByteString(strerror(errno)))
                           : AbsolutePath::FromString(FromByteString(result));
}

Path& Path::operator=(Path path) {
  this->path_ = std::move(path.path_);
  return *this;
}

Path::Path(std::wstring path) : path_(std::move(path)) {
  CHECK(!path_.empty());
}

Path Path::LocalDirectory() {
  return ValueOrDie(Path::FromString(L"."), L"Path::LocalDirectory");
}
Path Path::Root() { return ValueOrDie(Path::FromString(L"/"), L"Path::Root"); }

ValueOrError<AbsolutePath> AbsolutePath::FromString(std::wstring path) {
  if (path.empty()) {
    return Error(L"Path can't be empty");
  }
  if (path[0] != L'/') {
    return Error(L"Absolute path must start with /");
  }
  return Success(AbsolutePath(std::move(path)));
}

AbsolutePath::AbsolutePath(std::wstring path) : Path(std::move(path)) {}

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
  return Path::Join(ValueOrDie(Path::FromString(a), L"PathJoin"),
                    ValueOrDie(Path::FromString(b), L"PathJoin"))
      .read();
}

std::unique_ptr<DIR, std::function<void(DIR*)>> OpenDir(std::wstring path) {
  VLOG(10) << "Open dir: " << path;
  return std::unique_ptr<DIR, std::function<void(DIR*)>>(
      opendir(ToByteString(path).c_str()), closedir);
}

}  // namespace afc::infrastructure
