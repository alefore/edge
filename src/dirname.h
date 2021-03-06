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

#define GHOST_TYPE_EQ(ClassName, variable)        \
  bool operator==(const ClassName& other) const { \
    return variable == other.variable;            \
  }                                               \
  bool operator!=(const ClassName& other) const { return !(*this == other); }

class PathComponent {
 public:
  static ValueOrError<PathComponent> FromString(std::wstring component);
  static PathComponent WithExtension(const PathComponent& path,
                                     const std::wstring& extension);
  const std::wstring& ToString() const;

  GHOST_TYPE_EQ(PathComponent, component_);

  // "hey" => nullopt
  // "hey." => ""
  // "hey.xyz" => "xyz"
  std::optional<std::wstring> extension() const;

 private:
  friend class Path;
  explicit PathComponent(std::wstring component);

  const std::wstring component_;
};

class AbsolutePath;
class Path {
 public:
  Path(const Path&) = default;
  Path(Path&&) = default;
  Path(PathComponent path_component);

  static Path LocalDirectory();  // Similar to FromString(L".").
  static Path Root();            // Similar to FromString(L"/").

  static Path Join(Path a, Path b);
  static ValueOrError<Path> FromString(std::wstring path);
  static Path ExpandHomeDirectory(const Path& home_directory, const Path& path);

  // If an extension was already present, replaces it with the new value.
  static Path WithExtension(const Path& path, const std::wstring& extension);

  ValueOrError<Path> Dirname() const;
  ValueOrError<PathComponent> Basename() const;
  std::optional<std::wstring> extension() const;

  const std::wstring& ToString() const;
  ValueOrError<std::list<PathComponent>> DirectorySplit() const;
  bool IsRoot() const;

  enum class RootType { kAbsolute, kRelative };
  RootType GetRootType() const;

  ValueOrError<AbsolutePath> Resolve() const;

  Path& operator=(Path path);

  GHOST_TYPE_EQ(Path, path_);

 protected:
  explicit Path(std::wstring path);

 private:
  std::wstring path_;
};

class AbsolutePath : public Path {
 public:
  // Doesn't do any resolution; path must begin with '/'.
  static ValueOrError<AbsolutePath> FromString(std::wstring path);

 private:
  explicit AbsolutePath(std::wstring path);
};

std::ostream& operator<<(std::ostream& os, const PathComponent& p);
std::ostream& operator<<(std::ostream& os, const Path& p);

// TODO(easy): Remove these.
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
