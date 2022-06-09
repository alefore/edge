#include "src/infrastructure/dirname.h"

#include <cstring>

extern "C" {
#include <libgen.h>
}

#include <glog/logging.h>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::infrastructure {
using language::Error;
using language::FromByteString;
using language::NonNull;
using language::overload;
using language::Success;
using language::ToByteString;
using language::ValueOrDie;
using language::ValueOrError;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;

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
            CHECK_EQ(
                PathComponent::WithExtension(
                    ValueOrDie(PathComponent::FromString(L"foo"), L"tests"),
                    L"md"),
                ValueOrDie(PathComponent::FromString(L"foo.md"), L"tests"));
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK_EQ(
                PathComponent::WithExtension(
                    ValueOrDie(PathComponent::FromString(L"foo"), L"tests"),
                    L""),
                ValueOrDie(PathComponent::FromString(L"foo."), L"tests"));
          }},
     {.name = L"Present",
      .callback =
          [] {
            CHECK_EQ(
                PathComponent::WithExtension(
                    ValueOrDie(PathComponent::FromString(L"foo.txt"), L"tests"),
                    L"md"),
                ValueOrDie(PathComponent::FromString(L"foo.md"), L"tests"));
          }},
     {.name = L"MultipleReplacesOnlyLast", .callback = [] {
        CHECK_EQ(
            PathComponent::WithExtension(
                ValueOrDie(PathComponent::FromString(L"foo.blah.txt"),
                           L"tests"),
                L"md"),
            ValueOrDie(PathComponent::FromString(L"foo.blah.md"), L"tests"));
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
            CHECK(ValueOrDie(
                      ValueOrDie(PathComponent::FromString(L"foo"), L"tests")
                          .remove_extension(),
                      L"tests") ==
                  ValueOrDie(PathComponent::FromString(L"foo"), L"tests"));
          }},
     {.name = L"hidden",
      .callback =
          [] {
            CHECK(std::holds_alternative<Error>(
                ValueOrDie(PathComponent::FromString(L".blah"), L"tests")
                    .remove_extension()));
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(ValueOrDie(
                      ValueOrDie(PathComponent::FromString(L"foo."), L"tests")
                          .remove_extension(),
                      L"tests") ==
                  ValueOrDie(PathComponent::FromString(L"foo"), L"tests"));
          }},
     {.name = L"Present", .callback = [] {
        CHECK(ValueOrDie(
                  ValueOrDie(PathComponent::FromString(L"foo.md"), L"tests")
                      .remove_extension(),
                  L"tests") ==
              ValueOrDie(PathComponent::FromString(L"foo"), L"tests"));
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
            CHECK(!ValueOrDie(PathComponent::FromString(L"foo"), L"tests")
                       .extension()
                       .has_value());
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(ValueOrDie(PathComponent::FromString(L"foo."), L"tests")
                      .extension()
                      .value()
                      .empty());
          }},
     {.name = L"Present", .callback = [] {
        CHECK(ValueOrDie(PathComponent::FromString(L"foo.md"), L"tests")
                  .extension()
                  .value() == L"md");
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
  return Path::FromString(NewLazyString(std::move(path)));
}

ValueOrError<Path> Path::FromString(NonNull<std::shared_ptr<LazyString>> path) {
  // TODO(easy, 2022-06-10): Avoid call to ToString.
  return path->size().IsZero() ? Error(L"Empty path.")
                               : Success(Path(path->ToString()));
}

Path Path::ExpandHomeDirectory(const Path& home_directory, const Path& path) {
  // TODO: Also support ~user/foo.
  return std::visit(
      overload{[&](Error) { return path; },
               [&](std::list<PathComponent> components) {
                 if (components.empty() ||
                     components.front() !=
                         ValueOrDie(PathComponent::FromString(L"~"),
                                    L"ExpandHomeDirectory"))
                   return path;
                 components.pop_front();
                 auto output = home_directory;
                 for (auto& c : components) {
                   output = Path::Join(output, c);
                 }
                 return output;
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
  auto dir = path.Dirname();
  auto base = path.Basename();
  CHECK(!IsError(dir));
  CHECK(!IsError(base));
  return Path::Join(ValueOrDie(dir, L"Path::WithExtension"),
                    PathComponent::WithExtension(
                        ValueOrDie(base, L"Path::WithExtension"), extension));
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
    ASSIGN_OR_RETURN(auto dir, AugmentErrors(L"Dirname error", path.Dirname()));
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
               std::list<PathComponent> result = ValueOrDie(
                   ValueOrDie(Path::FromString(L"alejo.txt"), L"tests")
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(result.front(),
                        ValueOrDie(PathComponent::FromString(L"alejo.txt"),
                                   L"tests"));
             }},
        {.name = L"Directory",
         .callback =
             [] {
               auto result =
                   ValueOrDie(ValueOrDie(Path::FromString(L"alejo/"), L"tests")
                                  .DirectorySplit(),
                              L"tests");
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(
                   result.front(),
                   ValueOrDie(PathComponent::FromString(L"alejo"), L"tests"));
             }},
        {.name = L"LongSplit",
         .callback =
             [] {
               auto result_list = ValueOrDie(
                   ValueOrDie(Path::FromString(L"aaa/b/cc/ddd"), L"tests")
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result_list.size(), 4ul);
               std::vector<PathComponent> result(result_list.begin(),
                                                 result_list.end());
               CHECK_EQ(result[0], ValueOrDie(PathComponent::FromString(L"aaa"),
                                              L"tests"));
               CHECK_EQ(result[1],
                        ValueOrDie(PathComponent::FromString(L"b"), L"tests"));
               CHECK_EQ(result[2],
                        ValueOrDie(PathComponent::FromString(L"cc"), L"tests"));
               CHECK_EQ(result[3], ValueOrDie(PathComponent::FromString(L"ddd"),
                                              L"tests"));
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
               CHECK_EQ(result[0], ValueOrDie(PathComponent::FromString(L"aaa"),
                                              L"tests"));
               CHECK_EQ(result[1],
                        ValueOrDie(PathComponent::FromString(L"b"), L"tests"));
               CHECK_EQ(result[2],
                        ValueOrDie(PathComponent::FromString(L"cc"), L"tests"));
               CHECK_EQ(result[3], ValueOrDie(PathComponent::FromString(L"ddd"),
                                              L"tests"));
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
