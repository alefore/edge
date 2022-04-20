#ifndef __AFC_EDITOR_BUFFER_NAME_H__
#define __AFC_EDITOR_BUFFER_NAME_H__

#include "src/infrastructure/dirname.h"
#include "src/language/ghost_type.h"

namespace afc::editor {
class OpenBuffer;

class BufferName {
 public:
  // Name of the buffer that holds the contents that the paste command should
  // paste, which corresponds to things that have been deleted recently.
  static const BufferName& PasteBuffer();

  // Name of a special buffer that shows the list of buffers.
  static const BufferName& BuffersList();

  // Name of a special buffer that contains text being inserted.
  static const BufferName& TextInsertion();

  explicit BufferName(infrastructure::Path path);

  GHOST_TYPE_CONSTRUCTOR(BufferName, std::wstring, value);
  GHOST_TYPE_EQ(BufferName, value);
  GHOST_TYPE_LT(BufferName, value);
  GHOST_TYPE_OUTPUT_FRIEND(BufferName, value);
  GHOST_TYPE_HASH_FRIEND(BufferName, value);

  const std::wstring& read() const;

 private:
  std::wstring value;
};
using ::operator<<;
GHOST_TYPE_OUTPUT(BufferName, value);
}  // namespace afc::editor

GHOST_TYPE_HASH(afc::editor::BufferName)

#endif  // __AFC_EDITOR_BUFFER_NAME_H__
