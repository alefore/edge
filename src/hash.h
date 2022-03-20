#ifndef __AFC_EDITOR_SRC_HASH_H__
#define __AFC_EDITOR_SRC_HASH_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace afc::editor {
inline size_t hash_combine(size_t seed) { return seed; }

inline size_t hash_combine(size_t seed, size_t h) {
  return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

template <typename... Args>
inline size_t hash_combine(size_t seed, size_t h, Args... args) {
  return hash_combine(hash_combine(seed, h), args...);
}

// CallableWithCapture is used to bind arguments that a lambda form will need
// but including them in a hash.
//
// Instead of:
//
//   auto hash = hash_combine(a, b, c);
//   auto callable = [a, b, c]() { ... };
//
// Just:
//
//   auto callable = CaptureAndHash(
//       (A a, B b, C c) { ... },
//       a, b, c);
//
// The reason to do this is to make it less likely to incorrectly forget to
// include elements in the hash.
template <typename Callable>
struct CallableWithCapture {
  // The hash of bound elements that the callable will depend on.
  size_t hash;
  // A callable of an arbitrary type.
  Callable callable;
};

template <typename Callable, typename... Args>
auto CaptureAndHash(Callable callable, Args... args) {
  size_t hash = hash_combine(std::hash<decltype(args)>()(args)...);
  auto bound_callable = std::bind(callable, std::move(args)...);
  return CallableWithCapture<decltype(bound_callable)>{
      .hash = hash, .callable = std::move(bound_callable)};
}

// Wrapping in order to define a hash operator.
template <typename Container>
struct HashableContainer {
  HashableContainer(Container container) : container(std::move(container)) {}
  Container container;
};
}  // namespace afc::editor
namespace std {
template <typename Container>
struct hash<afc::editor::HashableContainer<Container>> {
  std::size_t operator()(
      const afc::editor::HashableContainer<Container>& container) const {
    size_t hash = 0;
    for (const auto& x : container.container) {
      using Element = typename std::remove_const<
          typename std::remove_reference<decltype(x)>::type>::type;
      hash = afc::editor::hash_combine(hash, std::hash<Element>{}(x));
    }
    return hash;
  }
};
}  // namespace std

#endif  // __AFC_EDITOR_SRC_HASH_H__
