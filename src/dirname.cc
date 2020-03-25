#include "src/dirname.h"

#include <cstring>

extern "C" {
#include <libgen.h>
}

#include <glog/logging.h>

#include "src/wstring.h"

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

const std::wstring& PathComponent::ToString() { return component_; }

PathComponent::PathComponent(std::wstring component)
    : component_(std::move(component)) {}

Path::Path(PathComponent path_component)
    : path_(std::move(path_component.component_)) {}

/* static */ Path Path::Join(Path a, Path b) {
  if (a.IsRoot() && b.IsRoot()) {
    return b;
  }
  bool has_slash = a.path_[a.path_.size() - 1] == L'/' || b.path_[0] == L'/';
  return Path::FromString(a.path_ + (has_slash ? L"" : L"/") + b.path_).value();
}

ValueOrError<Path> Path::FromString(std::wstring path) {
  return path.empty() ? Error(L"Empty path.") : Success(Path(std::move(path)));
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

const std::wstring& Path::ToString() const { return path_; }

ValueOrError<std::list<PathComponent>> Path::DirectorySplit() const {
  std::list<PathComponent> output;
  Path path = *this;
  while (!path.IsRoot()) {
    ASSIGN_OR_RETURN(auto base, path.Basename());
    output.push_front(base);
    if (output.front().ToString() == path.path_) {
      return Success(output);
    }
    ASSIGN_OR_RETURN(auto dir, AugmentErrors(L"Dirname error", path.Dirname()));
    if (dir.path_.size() >= path.path_.size()) {
      LOG(INFO) << "Unable to advance: " << path << " -> " << dir;
      return Error(L"Unable to advance: " + path.ToString());
    }
    path = std::move(dir);
  }
  return Success(output);
}

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
  os << p.ToString();
  return os;
}

wstring Realpath(const wstring& path) {
  auto output = Path::FromString(path).value().Resolve();
  return output.IsError() ? path : output.value().ToString();
}

wstring Dirname(wstring path_str) {
  auto path = Path::FromString(path_str);
  if (path.IsError()) return L"";
  auto output = path.value().Dirname();
  return output.IsError() ? L"" : output.value().ToString();
}

wstring Basename(wstring path_str) {
  auto path = Path::FromString(path_str);
  if (path.IsError()) return L"";
  auto output = path.value().Basename();
  return output.IsError() ? L"" : output.value().ToString();
}

bool DirectorySplit(wstring path_str, list<wstring>* output) {
  auto path = Path::FromString(path_str);
  if (path.IsError()) {
    return false;
  }
  auto components = path.value().DirectorySplit();
  if (components.IsError()) {
    return false;
  }
  output->clear();
  for (auto& component : components.value()) {
    output->push_back(component.ToString());
  }
  return true;
}

wstring PathJoin(const wstring& a, const wstring& b) {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  return Path::Join(Path::FromString(a).value(), Path::FromString(b).value())
      .ToString();
}

SplitExtensionOutput SplitExtension(const std::wstring& path) {
  auto index = path.find_last_of(L".");
  if (index == std::string::npos) {
    return {path, std::nullopt};
  }
  return {path.substr(0, index),
          SplitExtensionOutput::Suffix{path.substr(index, 1),
                                       path.substr(index + 1)}};
}

std::unique_ptr<DIR, std::function<void(DIR*)>> OpenDir(std::wstring path) {
  VLOG(10) << "Open dir: " << path;
  return std::unique_ptr<DIR, std::function<void(DIR*)>>(
      opendir(ToByteString(path).c_str()), closedir);
}

}  // namespace afc::editor
