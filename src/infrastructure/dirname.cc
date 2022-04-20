#include "src/infrastructure/dirname.h"

#include <cstring>

extern "C" {
#include <libgen.h>
}

#include <glog/logging.h>

#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::editor {

using std::list;
using std::wstring;

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
                         PathComponent::FromString(L"foo").value(), L"md"),
                     PathComponent::FromString(L"foo.md").value());
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo").value(), L""),
                     PathComponent::FromString(L"foo.").value());
          }},
     {.name = L"Present",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo.txt").value(), L"md"),
                     PathComponent::FromString(L"foo.md").value());
          }},
     {.name = L"MultipleReplacesOnlyLast", .callback = [] {
        CHECK_EQ(PathComponent::WithExtension(
                     PathComponent::FromString(L"foo.blah.txt").value(), L"md"),
                 PathComponent::FromString(L"foo.blah.md").value());
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
            CHECK(PathComponent::FromString(L"foo")
                      .value()
                      .remove_extension()
                      .value() == PathComponent::FromString(L"foo").value());
          }},
     {.name = L"hidden",
      .callback =
          [] {
            CHECK(PathComponent::FromString(L".blah")
                      .value()
                      .remove_extension()
                      .IsError());
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(PathComponent::FromString(L"foo.")
                      .value()
                      .remove_extension()
                      .value() == PathComponent::FromString(L"foo").value());
          }},
     {.name = L"Present", .callback = [] {
        CHECK(PathComponent::FromString(L"foo.md")
                  .value()
                  .remove_extension()
                  .value() == PathComponent::FromString(L"foo").value());
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
            CHECK(!PathComponent::FromString(L"foo")
                       .value()
                       .extension()
                       .has_value());
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(PathComponent::FromString(L"foo.")
                      .value()
                      .extension()
                      .value()
                      .empty());
          }},
     {.name = L"Present", .callback = [] {
        CHECK(
            PathComponent::FromString(L"foo.md").value().extension().value() ==
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
  return Path::FromString(a.path_ + (has_slash ? L"" : L"/") + b.path_).value();
}

const bool path_join_tests_registration = tests::Register(
    L"PathJoinTests",
    {{.name = L"LocalRedundant",
      .callback =
          [] {
            CHECK_EQ(Path::Join(Path::LocalDirectory(),
                                Path::FromString(L"alejo.txt").value()),
                     Path::FromString(L"alejo.txt").value());
          }},
     {.name = L"LocalImportant", .callback = [] {
        CHECK_EQ(Path::Join(Path::LocalDirectory(),
                            Path::FromString(L"/alejo.txt").value()),
                 Path::FromString(L"./alejo.txt").value());
      }}});

ValueOrError<Path> Path::FromString(std::wstring path) {
  return path.empty() ? Error(L"Empty path.") : Success(Path(std::move(path)));
}

Path Path::ExpandHomeDirectory(const Path& home_directory, const Path& path) {
  // TODO: Also support ~user/foo.
  if (ValueOrError<std::list<PathComponent>> components = path.DirectorySplit();
      !components.IsError() && !components.value().empty() &&
      components.value().front() == PathComponent::FromString(L"~").value()) {
    components.value().pop_front();
    auto output = home_directory;
    for (auto& c : components.value()) {
      output = Path::Join(output, c);
    }
    return output;
  }
  return path;
}

const bool expand_home_directory_tests_registration = tests::Register(
    L"ExpandHomeDirectoryTests",
    {
        {.name = L"NoExpansion",
         .callback =
             [] {
               CHECK_EQ(Path::ExpandHomeDirectory(
                            Path::FromString(L"/home/alejo").value(),
                            Path::FromString(L"foo/bar").value()),
                        Path::FromString(L"foo/bar").value());
             }},
        {.name = L"MinimalExpansion",
         .callback =
             [] {
               CHECK_EQ(Path::ExpandHomeDirectory(
                            Path::FromString(L"/home/alejo").value(),
                            Path::FromString(L"~").value()),
                        Path::FromString(L"/home/alejo").value());
             }},
        {.name = L"SmallExpansion",
         .callback =
             [] {
               CHECK_EQ(Path::ExpandHomeDirectory(
                            Path::FromString(L"/home/alejo").value(),
                            Path::FromString(L"~/").value()),
                        Path::FromString(L"/home/alejo").value());
             }},
        {.name = L"LongExpansion",
         .callback =
             [] {
               CHECK_EQ(Path::ExpandHomeDirectory(
                            Path::FromString(L"/home/alejo").value(),
                            Path::FromString(L"~/foo/bar").value()),
                        Path::FromString(L"/home/alejo/foo/bar").value());
             }},
        {.name = L"LongExpansionRedundantSlash",
         .callback =
             [] {
               CHECK_EQ(Path::ExpandHomeDirectory(
                            Path::FromString(L"/home/alejo/").value(),
                            Path::FromString(L"~/foo/bar").value()),
                        Path::FromString(L"/home/alejo/foo/bar").value());
             }},
    });

/* static */ Path Path::WithExtension(const Path& path,
                                      const std::wstring& extension) {
  auto dir = path.Dirname();
  auto base = path.Basename();
  CHECK(!dir.IsError());
  CHECK(!base.IsError());
  return Path::Join(dir.value(),
                    PathComponent::WithExtension(base.value(), extension));
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
  auto name = Basename();
  if (name.IsError()) return std::nullopt;
  return name.value().extension();
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
               auto result = Path::FromString(L"alejo.txt")
                                 .value()
                                 .DirectorySplit()
                                 .value();
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(result.front(),
                        PathComponent::FromString(L"alejo.txt").value());
             }},
        {.name = L"Directory",
         .callback =
             [] {
               auto result =
                   Path::FromString(L"alejo/").value().DirectorySplit().value();
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(result.front(),
                        PathComponent::FromString(L"alejo").value());
             }},
        {.name = L"LongSplit",
         .callback =
             [] {
               auto result_list = Path::FromString(L"aaa/b/cc/ddd")
                                      .value()
                                      .DirectorySplit()
                                      .value();
               CHECK_EQ(result_list.size(), 4ul);
               std::vector<PathComponent> result(result_list.begin(),
                                                 result_list.end());
               CHECK_EQ(result[0], PathComponent::FromString(L"aaa").value());
               CHECK_EQ(result[1], PathComponent::FromString(L"b").value());
               CHECK_EQ(result[2], PathComponent::FromString(L"cc").value());
               CHECK_EQ(result[3], PathComponent::FromString(L"ddd").value());
             }},
        {.name = L"LongSplitMultiSlash",
         .callback =
             [] {
               auto result_list = Path::FromString(L"aaa////b////cc/////ddd")
                                      .value()
                                      .DirectorySplit()
                                      .value();
               CHECK_EQ(result_list.size(), 4ul);
               std::vector<PathComponent> result(result_list.begin(),
                                                 result_list.end());
               CHECK_EQ(result[0], PathComponent::FromString(L"aaa").value());
               CHECK_EQ(result[1], PathComponent::FromString(L"b").value());
               CHECK_EQ(result[2], PathComponent::FromString(L"cc").value());
               CHECK_EQ(result[3], PathComponent::FromString(L"ddd").value());
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

Path Path::LocalDirectory() { return Path::FromString(L".").value(); }
Path Path::Root() { return Path::FromString(L"/").value(); }

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

wstring PathJoin(const wstring& a, const wstring& b) {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  return Path::Join(Path::FromString(a).value(), Path::FromString(b).value())
      .read();
}

std::unique_ptr<DIR, std::function<void(DIR*)>> OpenDir(std::wstring path) {
  VLOG(10) << "Open dir: " << path;
  return std::unique_ptr<DIR, std::function<void(DIR*)>>(
      opendir(ToByteString(path).c_str()), closedir);
}

}  // namespace afc::editor
