#ifndef __AFC_EDITOR_BUFFER_NAME_H__
#define __AFC_EDITOR_BUFFER_NAME_H__

#include "src/infrastructure/dirname.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/functional.h"

namespace afc::editor {
class OpenBuffer;

GHOST_TYPE(BufferFileId, infrastructure::Path);

// Name of the buffer that holds the contents that the paste command should
// paste, which corresponds to things that have been deleted recently.
struct PasteBuffer {
  bool operator==(const PasteBuffer&) const { return true; }
  bool operator<(const PasteBuffer&) const { return false; }
};

// Name of the buffer that holds the contents that have been deleted recently
// and which should still be included in the delete buffer for additional
// deletions.
//
// This is used so that multiple subsequent deletion transformations (without
// any interspersed non-delete transformations) will all aggregate into the
// paste buffer (rather than retaining only the deletion corresponding to the
// last such transformation).
struct FuturePasteBuffer {
  bool operator==(const FuturePasteBuffer&) const { return true; }
  bool operator<(const FuturePasteBuffer&) const { return false; }
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

struct InitialCommands {
  bool operator==(const InitialCommands&) const { return true; }
  bool operator<(const InitialCommands&) const { return false; }
};

struct ConsoleBufferName {
  bool operator==(const ConsoleBufferName&) const { return true; }
  bool operator<(const ConsoleBufferName&) const { return false; }
};

struct PredictionsBufferName {
  bool operator==(const PredictionsBufferName&) const { return true; }
  bool operator<(const PredictionsBufferName&) const { return false; }
};

GHOST_TYPE(ServerBufferName, infrastructure::Path);

GHOST_TYPE(CommandBufferName, language::lazy_string::LazyString);

GHOST_TYPE(AnonymousBufferName, size_t);

using BufferName =
    std::variant<BufferFileId, PasteBuffer, FuturePasteBuffer, BufferListId,
                 TextInsertion, InitialCommands, ConsoleBufferName,
                 PredictionsBufferName, ServerBufferName, CommandBufferName,
                 AnonymousBufferName, std::wstring>;

std::wstring to_wstring(const BufferName&);

std::ostream& operator<<(std::ostream& os, const BufferName& p);
}  // namespace afc::editor

GHOST_TYPE_TOP_LEVEL(afc::editor::BufferFileId);
GHOST_TYPE_TOP_LEVEL(afc::editor::ServerBufferName);
GHOST_TYPE_TOP_LEVEL(afc::editor::CommandBufferName);
GHOST_TYPE_TOP_LEVEL(afc::editor::AnonymousBufferName);

namespace std {
template <>
struct hash<afc::editor::PasteBuffer> {
  size_t operator()(const afc::editor::PasteBuffer&) const { return 0; }
};

template <>
struct hash<afc::editor::FuturePasteBuffer> {
  size_t operator()(const afc::editor::FuturePasteBuffer&) const { return 0; }
};

template <>
struct hash<afc::editor::BufferListId> {
  size_t operator()(const afc::editor::BufferListId&) const { return 0; }
};

template <>
struct hash<afc::editor::TextInsertion> {
  size_t operator()(const afc::editor::TextInsertion&) const { return 0; }
};

template <>
struct hash<afc::editor::InitialCommands> {
  size_t operator()(const afc::editor::InitialCommands&) const { return 0; }
};

template <>
struct hash<afc::editor::ConsoleBufferName> {
  size_t operator()(const afc::editor::ConsoleBufferName&) const { return 0; }
};

template <>
struct hash<afc::editor::PredictionsBufferName> {
  size_t operator()(const afc::editor::PredictionsBufferName&) const {
    return 0;
  }
};
}  // namespace std

#endif  // __AFC_EDITOR_BUFFER_NAME_H__
