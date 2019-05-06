#ifndef __AFC_EDITOR_SRC_HASH_H__
#define __AFC_EDITOR_SRC_HASH_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace editor {
template <class A, class B>
inline size_t hash_combine(const A& a, const B& b) {
  size_t output = std::hash<A>{}(a);
  output ^= std::hash<B>{}(b) + 0x9e3779b9 + (output << 6) + (output >> 2);
  return output;
}
template <class A, class B, class C>
inline size_t hash_combine(const A& a, const B& b, const C& c) {
  size_t output = std::hash<A>{}(a);
  output ^= std::hash<B>{}(b) + 0x9e3779b9 + (output << 6) + (output >> 2);
  output ^= std::hash<C>{}(c) + 0x9e3779b9 + (output << 6) + (output >> 2);
  return output;
}
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SRC_HASH_H__
