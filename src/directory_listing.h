#ifndef __AFC_EDITOR_DIRECTORY_LISTING_H__
#define __AFC_EDITOR_DIRECTORY_LISTING_H__

#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/value_or_error.h"

namespace afc::editor {
class OpenBuffer;
futures::Value<language::EmptyValue> GenerateDirectoryListing(
    infrastructure::Path path, OpenBuffer& output);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_DIRECTORY_LISTING_H__
