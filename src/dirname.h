#ifndef __AFC_EDITOR_DIRNAME_H__
#define __AFC_EDITOR_DIRNAME_H__

#include <wchar.h>

#include <list>
#include <memory>
#include <optional>

#include "command.h"

namespace afc {
namespace editor {

std::wstring Dirname(std::wstring path);
std::wstring Basename(std::wstring path);
bool DirectorySplit(std::wstring path, std::list<std::wstring>* output);
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);

struct SplitExtensionOutput {
  std::wstring prefix;  // "foo/bar.hey" => "foo/bar".
  struct Suffix {
    std::wstring separator;
    std::wstring extension;
  };
  std::optional<Suffix> suffix;
};
SplitExtensionOutput SplitExtension(const std::wstring& path);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_DIRNAME_H__
