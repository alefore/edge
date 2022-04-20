#ifndef __AFC_EDITOR_DIRECTORY_LISTING_H__
#define __AFC_EDITOR_DIRECTORY_LISTING_H__

#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"

namespace afc::editor {
class OpenBuffer;
futures::Value<EmptyValue> GenerateDirectoryListing(Path path,
                                                    OpenBuffer& output);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_DIRECTORY_LISTING_H__
