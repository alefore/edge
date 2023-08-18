#ifndef __AFC_VM_STRUCTURE_H__
#define __AFC_VM_STRUCTURE_H__

#include <string>

#include "src/direction.h"
#include "src/line_column.h"
#include "src/parse_tree.h"

namespace afc {
namespace editor {

class Modifiers;
class DeleteOptions;
class Transformation;
class OpenBuffer;
class BufferContents;
class OperationScopeBufferInformation;
class ParseTree;

class Structure {
 public:
  virtual std::wstring ToString() = 0;
  virtual Structure* Lower() = 0;

  // Controls the behavior when a range is selected while the cursor is in a
  // space (in a position between two instances of the structure, that doesn't
  // belong to either).
  enum class SpaceBehavior {
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

  virtual SpaceBehavior space_behavior() = 0;

  enum class SearchQuery {
    kPrompt,  // Prompt the user for the search string.
    kRegion,  // The current region is the query to search for.
  };

  virtual SearchQuery search_query() = 0;

  enum class SearchRange {
    kBuffer,  // The search should go over the entire buffer.
    kRegion,  // The search should be constrained to the current region.
  };

  virtual SearchRange search_range() = 0;

  // Moves position in the specified direction until we're inside the structure
  // of the type specified that starts after position. No-op if we're already
  // inside the structure.
  struct SeekInput {
    const BufferContents& contents;
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
  virtual void SeekToNext(SeekInput input) = 0;

  // Moves position in the specified direction until we're just outside of the
  // current structure of the type specified. No-op if we're already outside the
  // structure. Returns a boolean indicating whether it successfully found a
  // position outside of the structure.
  virtual bool SeekToLimit(SeekInput input) = 0;

  virtual std::optional<LineColumn> Move(
      const OperationScopeBufferInformation& scope,
      const BufferContents& contents, LineColumn position, Range range,
      const Modifiers& modifiers) = 0;
};

Structure* StructureChar();
Structure* StructureWord();
Structure* StructureSymbol();
Structure* StructureLine();
Structure* StructureMark();
Structure* StructurePage();
Structure* StructureSearch();
Structure* StructureTree();
Structure* StructureCursor();
Structure* StructureSentence();
Structure* StructureParagraph();
Structure* StructureBuffer();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_VM_STRUCTURE_H__
