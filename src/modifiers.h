#ifndef __AFC_EDITOR_MODIFIERS_H__
#define __AFC_EDITOR_MODIFIERS_H__

#include <iostream> 

#include "direction.h"
#include "structure.h"

namespace afc {
namespace editor {

using std::ostream;

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
};

ostream& operator<<(ostream& os, const Modifiers& m);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_MODIFIERS_H__
