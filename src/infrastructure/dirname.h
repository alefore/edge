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
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::infrastructure {

struct PathComponentValidator {
  static language::PossibleError Validate(
      const language::lazy_string::LazyString& input);
};

class PathComponent
    : public language::GhostType<PathComponent,
                                 language::lazy_string::LazyString,
                                 PathComponentValidator> {
 public:
  using GhostType::GhostType;

  // Compile-time version of `New` for string literals.
  template <size_t N>
  static PathComponent FromString(const wchar_t (&str)[N]) {
    static_assert(N > 1, "String cannot be empty");
    // TODO(2024-08-02): This should also validate no slashes!
    return PathComponent{language::lazy_string::LazyString{str}};
  }

  static PathComponent WithExtension(
      const PathComponent& path,
      const language::lazy_string::LazyString& extension);

  // Can fail for ".md".
  language::ValueOrError<PathComponent> remove_extension() const;

  // "hey" => nullopt
  // "hey." => ""
  // "hey.xyz" => "xyz"
  std::optional<language::lazy_string::LazyString> extension() const;
};

struct PathValidator {
  static language::PossibleError Validate(
      const language::lazy_string::LazyString& path);
};

class AbsolutePath;
class Path : public language::GhostType<Path, language::lazy_string::LazyString,
                                        PathValidator> {
 public:
  using GhostType::GhostType;

  Path(PathComponent path_component);

  static Path LocalDirectory();  // Similar to FromString(L".").
  static Path Root();            // Similar to FromString(L"/").

  static Path Join(Path a, Path b);
  static Path ExpandHomeDirectory(const Path& home_directory, const Path& path);

  // If an extension was already present, replaces it with the new value.
  static Path WithExtension(const Path& path,
                            const language::lazy_string::LazyString& extension);

  language::ValueOrError<Path> Dirname() const;
  language::ValueOrError<PathComponent> Basename() const;
  std::optional<language::lazy_string::LazyString> extension() const;

  language::ValueOrError<std::list<PathComponent>> DirectorySplit() const;
  bool IsRoot() const;

  enum class RootType { kAbsolute, kRelative };
  RootType GetRootType() const;

  language::ValueOrError<AbsolutePath> Resolve() const;
};

class AbsolutePath : public Path {
 public:
  // Doesn't do any resolution; path must begin with '/'.
  static language::ValueOrError<AbsolutePath> FromString(
      language::lazy_string::LazyString path);

 private:
  explicit AbsolutePath(language::lazy_string::LazyString path);
};

// TODO(easy): Remove this:
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);

// Wrapper around `opendir` that calls `closedir` in the deleter.
language::ValueOrError<
    language::NonNull<std::unique_ptr<DIR, std::function<void(DIR*)>>>>
OpenDir(Path path);
}  // namespace afc::infrastructure

#endif  // __AFC_EDITOR_DIRNAME_H__
