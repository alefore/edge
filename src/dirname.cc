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

wstring Realpath(const wstring& path) {
  char* result = realpath(ToByteString(path).c_str(), nullptr);
  return result == nullptr ? path : FromByteString(result);
}

wstring Dirname(wstring path) {
  VLOG(5) << "Dirname: " << path;
  std::unique_ptr<char, decltype(&std::free)> tmp(
      strdup(ToByteString(path).c_str()), &std::free);
  CHECK(tmp != nullptr);
  return FromByteString(dirname(tmp.get()));
}

wstring Basename(wstring path) {
  VLOG(5) << "Pathname: " << path;
  std::unique_ptr<char, decltype(&std::free)> tmp(
      strdup(ToByteString(path).c_str()), &std::free);
  CHECK(tmp != nullptr);
  return FromByteString(basename(tmp.get()));
}

bool DirectorySplit(wstring path, list<wstring>* output) {
  output->clear();
  while (!path.empty() && path != L"/") {
    output->push_front(Basename(path));
    if (output->front() == path) {
      return true;
    }
    auto tmp = Dirname(path);
    if (tmp.size() >= path.size()) {
      LOG(INFO) << "Unable to advance: " << path << " -> " << tmp;
      return false;
    }
    path = tmp;
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
  if (a == L"/" && b[0] == L'/') {
    return b;
  }
  bool has_slash = a[a.size() - 1] == L'/' || b[0] == L'/';
  return a + (has_slash ? L"" : L"/") + b;
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
