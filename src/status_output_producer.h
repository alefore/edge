#ifndef __AFC_EDITOR_STATUS_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_STATUS_OUTPUT_PRODUCER_H__

#include <cwchar>
#include <list>
#include <memory>
#include <string>

#include "src/line_with_cursor.h"
#include "src/modifiers.h"

namespace afc {
namespace editor {

class Status;
class OpenBuffer;

struct StatusOutputOptions {
  const Status& status;

  // `buffer` will be null if this status isn't associated with a specific
  // buffer (i.e., if it's the editor's status).
  const OpenBuffer* buffer;
  Modifiers modifiers;

  // Size is the maximum size to generate; we may generate fewer lines (e.g., if
  // the status is empty).
  LineColumnDelta size;
};

LineWithCursor::Generator::Vector StatusOutput(StatusOutputOptions options);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_STATUS_OUTPUT_PRODUCER_H__
