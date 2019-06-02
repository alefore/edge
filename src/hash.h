#ifndef __AFC_EDITOR_SRC_HASH_H__
#define __AFC_EDITOR_SRC_HASH_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace editor {
inline size_t hash_combine(size_t seed, size_t h) {
  return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

template <typename... Args>
inline size_t hash_combine(size_t seed, size_t h, Args... args) {
  return hash_combine(hash_combine(seed, h), args...);
}

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SRC_HASH_H__
