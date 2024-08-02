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

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::infrastructure {

class PathComponent : public language::GhostType<PathComponent, std::wstring> {
 public:
  // Compile-time version of FromString for string literals.
  template <size_t N>
  static PathComponent FromString(const wchar_t (&str)[N]) {
    static_assert(N > 1, "String cannot be empty");
    return PathComponent{std::wstring{str}};
  }

  static language::PossibleError Validate(const std::wstring& input);

  // TODO(trivial, 2024-08-01): Get rid of this method. Just use `New`. Should
  // probably add some good tests for it.
  static language::ValueOrError<PathComponent> FromString(
      std::wstring component);
  static PathComponent WithExtension(const PathComponent& path,
                                     const std::wstring& extension);

  // TODO(trivial, 2024-08-02): Get rid of this, just use to_wstring.
  const std::wstring& ToString() const;

  // Can fail for ".md".
  language::ValueOrError<PathComponent> remove_extension() const;

  // "hey" => nullopt
  // "hey." => ""
  // "hey.xyz" => "xyz"
  std::optional<std::wstring> extension() const;
};

class AbsolutePath;
class Path {
 public:
  using ValueType = std::wstring;

  Path(const Path&) = default;
  Path(Path&&) = default;
  Path(PathComponent path_component);

  static Path LocalDirectory();  // Similar to FromString(L".").
  static Path Root();            // Similar to FromString(L"/").

  static Path Join(Path a, Path b);
  static language::ValueOrError<Path> FromString(std::wstring path);
  static language::ValueOrError<Path> FromString(
      language::lazy_string::LazyString path);
  static Path ExpandHomeDirectory(const Path& home_directory, const Path& path);

  // If an extension was already present, replaces it with the new value.
  static Path WithExtension(const Path& path, const std::wstring& extension);

  language::ValueOrError<Path> Dirname() const;
  language::ValueOrError<PathComponent> Basename() const;
  std::optional<std::wstring> extension() const;

  const std::wstring& read() const;
  language::ValueOrError<std::list<PathComponent>> DirectorySplit() const;
  bool IsRoot() const;

  enum class RootType { kAbsolute, kRelative };
  RootType GetRootType() const;

  language::ValueOrError<AbsolutePath> Resolve() const;

  Path& operator=(Path path);

  GHOST_TYPE_EQ(Path, path_);
  GHOST_TYPE_ORDER(Path, path_);
  GHOST_TYPE_HASH_FRIEND(Path, path_);

 protected:
  explicit Path(std::wstring path);

 private:
  ValueType path_;
};

class AbsolutePath : public Path {
 public:
  // Doesn't do any resolution; path must begin with '/'.
  static language::ValueOrError<AbsolutePath> FromString(std::wstring path);

 private:
  explicit AbsolutePath(std::wstring path);
};

std::ostream& operator<<(std::ostream& os, const Path& p);

// TODO(easy): Remove this:
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);

// Wrapper around `opendir` that calls `closedir` in the deleter.
std::unique_ptr<DIR, std::function<void(DIR*)>> OpenDir(std::wstring path);

// TODO(trivial, 2024-08-01): Convert `Path` to GhostType<> and remove this.
inline std::wstring to_wstring(const afc::infrastructure::Path& obj) {
  return obj.read();
}
}  // namespace afc::infrastructure

GHOST_TYPE_HASH(afc::infrastructure::Path);
#endif  // __AFC_EDITOR_DIRNAME_H__
