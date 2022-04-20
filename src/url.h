#ifndef __AFC_EDITOR_URL_H__
#define __AFC_EDITOR_URL_H__

#include "src/infrastructure/dirname.h"
#include "src/language/ghost_type.h"
#include "src/language/value_or_error.h"

namespace afc::editor {

class URL {
 public:
  GHOST_TYPE_CONSTRUCTOR(URL, std::wstring, value_);

  static URL FromPath(infrastructure::Path path);

  GHOST_TYPE_EQ(URL, value_);
  GHOST_TYPE_LT(URL, value_);

  enum class Schema { kFile, kHttp, kHttps };
  std::optional<Schema> schema() const;

  language::ValueOrError<infrastructure::Path> GetLocalFilePath() const;

  std::wstring ToString() const;

 private:
  std::wstring value_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_URL_H__
