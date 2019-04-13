#include "src/dirname.h"

#include <cstring>

extern "C" {
#include <libgen.h>
}

#include <glog/logging.h>

#include "src/wstring.h"

namespace afc {
namespace editor {

using std::list;
using std::wstring;

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
  bool has_slash = a[a.size() - 1] == L'/' || b[0] == L'/';
  return a + (has_slash ? L"" : L"/") + b;
}

}  // namespace editor
}  // namespace afc
