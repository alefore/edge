#ifndef __AFC_EDITOR_MODIFIERS_H__
#define __AFC_EDITOR_MODIFIERS_H__

#include <iostream> 
#include <map>
#include <set>
#include <string>
#include <vector>

#include "direction.h"
#include "line_column.h"
#include "structure.h"
#include "tree.h"

namespace afc {
namespace editor {

using std::ostream;

struct BufferPosition {
  // The name of the buffer.
  std::wstring buffer_name;
  // The position within the buffer.
  LineColumn position;
};

std::ostream& operator<<(std::ostream& os, const BufferPosition& bp);

struct Modifiers {
  enum StructureRange {
    ENTIRE_STRUCTURE,
    FROM_BEGINNING_TO_CURRENT_POSITION,
    FROM_CURRENT_POSITION_TO_END,
  };

  enum Strength {
    VERY_WEAK,
    WEAK,
    DEFAULT,
    STRONG,
    VERY_STRONG,
  };

  enum Insertion {
    // Default.  Text inserted pushes previous contents backwards.
    INSERT,
    // Text inserted overwrites previous contents.
    REPLACE
  };

  // Sets the modifiers to their default values, including resetting any form
  // of stickyness.
  void ResetHard() {
    structure = CHAR;
    structure_range = Modifiers::ENTIRE_STRUCTURE;
    default_direction = FORWARDS;
    default_insertion = INSERT;
    ResetSoft();
  }

  // After executing a command, sets modifiers to their default values, but,
  // unline ResetHard, abides by stickyness.
  void ResetSoft() {
    ResetStructure();
    strength = DEFAULT;
    ResetDirection();
    ResetInsertion();
    ResetRepetitions();
  }

  void ResetStructure() {
    if (!sticky_structure) {
      structure = Structure::CHAR;
    }
    structure_range = ENTIRE_STRUCTURE;
  }

  void ResetDirection() { direction = default_direction; }

  void ResetInsertion() { insertion = default_insertion; }

  void ResetRepetitions() { repetitions = 1; }

  // Fields follow.
  Structure structure = CHAR;
  StructureRange structure_range = ENTIRE_STRUCTURE;
  bool sticky_structure = false;

  Strength strength = DEFAULT;

  Direction direction = FORWARDS;
  Direction default_direction = FORWARDS;

  Insertion insertion = INSERT;
  Insertion default_insertion = INSERT;

  size_t repetitions = 1;

  // The start of the region. region_start is only defined if has_region_start
  // is true.
  bool has_region_start = false;
  BufferPosition region_start;

  // The currently active cursors.
  std::wstring active_cursors;
};

ostream& operator<<(ostream& os, const Modifiers& m);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_MODIFIERS_H__
