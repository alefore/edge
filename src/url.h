#ifndef __AFC_EDITOR_URL_H__
#define __AFC_EDITOR_URL_H__

#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::editor {

class URL : public language::GhostType<URL, language::lazy_string::LazyString> {
 public:
  static URL FromPath(infrastructure::Path path);

  enum class Schema { kFile, kHttp, kHttps };
  std::optional<Schema> schema() const;

  language::ValueOrError<infrastructure::Path> GetLocalFilePath() const;

  // TODO(easy, 2024-08-02): Change customers to to_wstring?
  language::lazy_string::LazyString ToString() const;
};

// If `url` is a local file, returns a vector with variations adding all the
// extensions from a list of extensions (given as a space-separated list).
std::vector<URL> GetLocalFileURLsWithExtensions(
    const language::lazy_string::LazyString& file_context_extensions,
    const URL& url);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_URL_H__
