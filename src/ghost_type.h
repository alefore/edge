#ifndef __AFC_EDITOR_GHOST_TYPE_H__
#define __AFC_EDITOR_GHOST_TYPE_H__

namespace afc::editor {
#define GHOST_TYPE_EQ(ClassName, variable)        \
  bool operator==(const ClassName& other) const { \
    return variable == other.variable;            \
  }                                               \
  bool operator!=(const ClassName& other) const { return !(*this == other); }

#define GHOST_TYPE_LT(ClassName, variable)       \
  bool operator<(const ClassName& other) const { \
    return variable < other.variable;            \
  }
}  // namespace afc::editor

#endif  //__AFC_EDITOR_GHOST_TYPE_H__
