#ifndef __AFC_EDITOR_DIRNAME_H__
#define __AFC_EDITOR_DIRNAME_H__

#include <wchar.h>

#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>

extern "C" {
#include <dirent.h>
}

#include "src/command.h"
#include "src/value_or_error.h"

namespace afc::editor {

class PathComponent {
 public:
  static ValueOrError<PathComponent> FromString(std::wstring component);
  const std::wstring& ToString();

 private:
  PathComponent(std::wstring component);
  const std::wstring component_;
};

class AbsolutePath;
class Path {
 public:
  Path(const Path&) = default;
  Path(Path&&) = default;

  static Path Join(Path a, Path b);
  static ValueOrError<Path> FromString(std::wstring path);
  ValueOrError<Path> Dirname() const;
  ValueOrError<PathComponent> Basename() const;
  const std::wstring& ToString() const;
  ValueOrError<std::list<PathComponent>> DirectorySplit() const;
  bool IsRoot() const;

  ValueOrError<AbsolutePath> Resolve() const;

  Path& operator=(Path path);

 protected:
  Path(std::wstring path);

 private:
  std::wstring path_;
};

class AbsolutePath : public Path {
 public:
  static ValueOrError<AbsolutePath> FromString(std::wstring path);

 private:
  AbsolutePath(std::wstring path);
};

std::ostream& operator<<(std::ostream& os, const Path& p);

// TODO(easy): Remove these.
std::wstring Realpath(const std::wstring& path);
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

// Wrapper around `opendir` that calls `closedir` in the deleter.
std::unique_ptr<DIR, std::function<void(DIR*)>> OpenDir(std::wstring path);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_DIRNAME_H__
