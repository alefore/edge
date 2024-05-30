#ifndef __AFC_EDITOR_BUFFER_NAME_H__
#define __AFC_EDITOR_BUFFER_NAME_H__

#include "src/infrastructure/dirname.h"
#include "src/language/ghost_type.h"

namespace afc::editor {
class OpenBuffer;

GHOST_TYPE(BufferFileId, infrastructure::Path);

// Name of the buffer that holds the contents that the paste command should
// paste, which corresponds to things that have been deleted recently.
struct PasteBuffer {
  bool operator==(const PasteBuffer&) const { return true; }
  bool operator<(const PasteBuffer&) const { return false; }
};

// Name of a special buffer that shows the list of buffers.
struct BufferListId {
  bool operator==(const BufferListId&) const { return true; }
  bool operator<(const BufferListId&) const { return false; }
};

// Name of a special buffer that contains text being inserted.
struct TextInsertion {
  bool operator==(const TextInsertion&) const { return true; }
  bool operator<(const TextInsertion&) const { return false; }
};

using BufferName = std::variant<BufferFileId, PasteBuffer, BufferListId,
                                TextInsertion, std::wstring>;

std::wstring to_wstring(const BufferName&);

std::ostream& operator<<(std::ostream& os, const BufferName& p);
}  // namespace afc::editor

GHOST_TYPE_TOP_LEVEL(afc::editor::BufferFileId);

namespace std {
template <>
struct hash<afc::editor::PasteBuffer> {
  size_t operator()(const afc::editor::PasteBuffer&) const { return 0; }
};

template <>
struct hash<afc::editor::BufferListId> {
  size_t operator()(const afc::editor::BufferListId&) const { return 0; }
};

template <>
struct hash<afc::editor::TextInsertion> {
  size_t operator()(const afc::editor::TextInsertion&) const { return 0; }
};
}  // namespace std

#endif  // __AFC_EDITOR_BUFFER_NAME_H__
