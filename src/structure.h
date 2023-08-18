#ifndef __AFC_VM_STRUCTURE_H__
#define __AFC_VM_STRUCTURE_H__

#include <string>

#include "src/direction.h"
#include "src/line_column.h"
#include "src/parse_tree.h"

namespace afc {
namespace editor {
class DeleteOptions;
class Transformation;
class OpenBuffer;
class BufferContents;
class ParseTree;

enum class Structure {
  kChar,
  kWord,
  kSymbol,
  kLine,
  kMark,
  kPage,
  kSearch,
  kTree,
  kCursor,
  kSentence,
  kParagraph,
  kBuffer,
};

std::ostream& operator<<(std::ostream& os, const Structure& structure);
// TODO(trivial, 2023-08-18): Get rid of this; callers should use operator<<.
std::wstring ToString(Structure);

Structure StructureLower(Structure);

// Controls the behavior when a range is selected while the cursor is in a
// space (in a position between two instances of the structure, that doesn't
// belong to either).
enum class StructureSpaceBehavior {
  // The spaces that follow the end of the structure are considered part of
  // the preceding instance of the structure. This is used by lines: the last
  // character in each line is considered the separator. However, if we start
  // there, we're deleting the current line (i.e., the space belongs to the
  // preceding line).
  kBackwards,
  // The spaces that follow the end of the structure aren't considered part of
  // the preceding instance of the structure. If a range selection starts in a
  // space, it will include the following instance.
  kForwards
};

StructureSpaceBehavior GetStructureSpaceBehavior(Structure structure);

enum class StructureSearchQuery {
  kPrompt,  // Prompt the user for the search string.
  kRegion,  // The current region is the query to search for.
};

StructureSearchQuery GetStructureSearchQuery(Structure structure);

enum class StructureSearchRange {
  kBuffer,  // The search should go over the entire buffer.
  kRegion,  // The search should be constrained to the current region.
};

StructureSearchRange GetStructureSearchRange(Structure structure);

// Moves position in the specified direction until we're inside the structure
// of the type specified that starts after position. No-op if we're already
// inside the structure.
struct SeekInput {
  const BufferContents& contents;
  Structure structure;
  Direction direction;
  std::wstring line_prefix_characters;
  std::wstring symbol_characters;
  language::NonNull<std::shared_ptr<const ParseTree>> parse_tree;
  const CursorsSet* cursors;
  // Input-output parameter.
  //
  // TODO(easy, 2023-08-17): Replace with an output parameter.
  LineColumn* position;
};
void SeekToNext(SeekInput input);

// Moves position in the specified direction until we're just outside of the
// current structure of the type specified. No-op if we're already outside the
// structure. Returns a boolean indicating whether it successfully found a
// position outside of the structure.
bool SeekToLimit(SeekInput input);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_VM_STRUCTURE_H__
