#ifndef __AFC_EDITOR_DIRECTORY_CACHE_H__
#define __AFC_EDITOR_DIRECTORY_CACHE_H__

#include <cwchar>
#include <list>
#include <memory>
#include <string>

#include "editor.h"
#include "output_producer.h"
#include "screen.h"

namespace afc {
namespace editor {

// The results of searching for files that match a given pattern.
struct DirectoryCacheOutput {
  // The total number of entries matched.
  int count = 0;

  // The longest substring of the pattern that matches at least one entry.
  //
  // For example, if directory `foo/bar` has files `alejo` and
  // `alejandro`, searching for `foo/bar/alhambra` will contain `foo/bar/al`.
  std::wstring longest_prefix;

  // If longest_valid_prefix contains the entire pattern, the longest possible
  // string that could be appended, such that the number of matches wouldn't
  // change.
  //
  // For example, if directory `foo/bar` has files `alejo` and `alejandro`,
  // searching for `foo/bar/al` will contain `ej`.
  std::wstring longest_suffix;
};

struct DirectoryCacheInput {
  std::wstring pattern;
  std::function<void(const DirectoryCacheOutput&)> callback;
};

AsyncProcessor<DirectoryCacheInput, DirectoryCacheOutput> NewDirectoryCache();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_DIRECTORY_CACHE_H__
