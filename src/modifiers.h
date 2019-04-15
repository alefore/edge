#ifndef __AFC_EDITOR_MODIFIERS_H__
#define __AFC_EDITOR_MODIFIERS_H__

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/direction.h"
#include "src/line_column.h"
#include "src/structure.h"
#include "src/tree.h"
#include "src/vm/public/environment.h"

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
  static void Register(vm::Environment* environment);

  enum StructureRange {
    ENTIRE_STRUCTURE,
    FROM_BEGINNING_TO_CURRENT_POSITION,
    FROM_CURRENT_POSITION_TO_END,
  };

  enum class Strength {
    kNormal,
    kStrong,
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
    structure = StructureChar();
    structure_range = Modifiers::ENTIRE_STRUCTURE;
    default_direction = FORWARDS;
    default_insertion = INSERT;
    ResetSoft();
  }

  // After executing a command, sets modifiers to their default values, but,
  // unline ResetHard, abides by stickyness.
  void ResetSoft() {
    ResetStructure();
    ResetDirection();
    strength = Strength::kNormal;
    ResetInsertion();
    ResetRepetitions();
  }

  void ResetStructure() {
    if (!sticky_structure) {
      structure = StructureChar();
    }
    structure_range = ENTIRE_STRUCTURE;
  }

  void ResetDirection() { direction = default_direction; }

  void ResetInsertion() { insertion = default_insertion; }

  void ResetRepetitions() { repetitions = 1; }

  // Fields follow.
  Structure* structure = StructureChar();
  StructureRange structure_range = ENTIRE_STRUCTURE;
  bool sticky_structure = false;

  Strength strength = Strength::kNormal;

  Direction direction = FORWARDS;
  Direction default_direction = FORWARDS;

  Insertion insertion = INSERT;
  Insertion default_insertion = INSERT;

  size_t repetitions = 1;

  enum DeleteType {
    DELETE_CONTENTS,
    PRESERVE_CONTENTS,
  };
  DeleteType delete_type = DELETE_CONTENTS;

  enum Boundary {
    // At the current cursor position.
    CURRENT_POSITION,

    // Strictly at the start/end of the current region.
    LIMIT_CURRENT,

    // At the start/end of the next region.
    LIMIT_NEIGHBOR,
  };

  Boundary boundary_begin = CURRENT_POSITION;
  Boundary boundary_end = LIMIT_CURRENT;

  enum CursorsAffected {
    // The transformation only affects the current cursor.
    AFFECT_ONLY_CURRENT_CURSOR,

    // The transformation affects all cursors.
    AFFECT_ALL_CURSORS,
  };

  CursorsAffected cursors_affected = AFFECT_ONLY_CURRENT_CURSOR;

  // The currently active cursors.
  std::wstring active_cursors;
};

Modifiers::Boundary IncrementBoundary(Modifiers::Boundary boundary);

ostream& operator<<(ostream& os, const Modifiers& m);

}  // namespace editor
namespace vm {
template <>
struct VMTypeMapper<editor::Modifiers*> {
  static editor::Modifiers* get(Value* value);
  static Value::Ptr New(editor::Modifiers* value);
  static const VMType vmtype;
};
}  // namespace vm
}  // namespace afc

#endif  // __AFC_EDITOR_MODIFIERS_H__
