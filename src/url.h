#ifndef __AFC_EDITOR_URL_H__
#define __AFC_EDITOR_URL_H__

#include "src/dirname.h"
#include "src/ghost_type.h"
#include "src/value_or_error.h"

namespace afc::editor {

class URL {
 public:
  GHOST_TYPE_CONSTRUCTOR(URL, value_);

  static URL FromPath(Path path);

  GHOST_TYPE_EQ(URL, value_);
  GHOST_TYPE_LT(URL, value_);

  enum class Schema { kFile, kHttp, kHttps };
  std::optional<Schema> schema() const;

  ValueOrError<Path> GetLocalFilePath() const;

 private:
  std::wstring value_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_URL_H__
