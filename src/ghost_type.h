#ifndef __AFC_EDITOR_GHOST_TYPE_H__
#define __AFC_EDITOR_GHOST_TYPE_H__

#include <functional>

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

#define GHOST_TYPE_BEGIN_END(ClassName, variable) \
  auto begin() const { return variable.begin(); } \
  auto end() const { return variable.end(); }

#define GHOST_TYPE_HASH_FRIEND(ClassName, variable) \
  friend class std::hash<ClassName>;

#define GHOST_TYPE_HASH(ClassName, variable)              \
  template <>                                             \
  struct std::hash<ClassName> {                           \
    std::size_t operator()(const ClassName& self) const { \
      return std::hash<std::wstring>{}(self.variable);    \
    }                                                     \
  };

}  // namespace afc::editor

#endif  //__AFC_EDITOR_GHOST_TYPE_H__
