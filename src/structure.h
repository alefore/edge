#ifndef __AFC_VM_STRUCTURE_H__
#define __AFC_VM_STRUCTURE_H__

namespace afc {
namespace editor {

// TODO: Move to Modifiers.
enum Structure {
  CHAR,
  WORD,
  LINE,
  MARK,
  PAGE,
  SEARCH,
  TREE,
  CURSOR,
  PARAGRAPH,
  BUFFER,
};

Structure LowerStructure(Structure s);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_VM_STRUCTURE_H__
