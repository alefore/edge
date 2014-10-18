#ifndef __AFC_VM_STRUCTURE_H__
#define __AFC_VM_STRUCTURE_H__

namespace afc {
namespace editor {

enum Structure {
  CHAR,
  WORD,
  LINE,
  PAGE,
  SEARCH,
  BUFFER,
};

enum StructureModifier {
  ENTIRE_STRUCTURE,
  FROM_BEGINNING_TO_CURRENT_POSITION,
  FROM_CURRENT_POSITION_TO_END,
};

Structure LowerStructure(Structure s);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_VM_STRUCTURE_H__
