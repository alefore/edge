#ifndef __AFC_EDITOR_SRC_HASH_H__
#define __AFC_EDITOR_SRC_HASH_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace afc::language {
inline size_t hash_combine(size_t seed) { return seed; }

inline size_t hash_combine(size_t seed, size_t h) {
  return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

template <typename... Args>
inline size_t hash_combine(size_t seed, size_t h, Args... args) {
  return hash_combine(hash_combine(seed, h), args...);
}

// Convenience function to compute the hash of an object.
template <typename A, typename... Args>
inline size_t compute_hash(const A& a) {
  using Element = typename std::remove_const<
      typename std::remove_reference<decltype(a)>::type>::type;
  return std::hash<Element>{}(a);
}

// Convenience function to compute the hash from a sequence of objects.
template <typename A, typename... Args>
inline size_t compute_hash(const A& a, const Args&... args) {
  return hash_combine(compute_hash(a), compute_hash(args)...);
}

template <typename Iterator, typename Callable>
struct HashableIteratorRange {
  Iterator begin;
  Iterator end;

  // Callable must be a callable that receives a const ref to the objects
  // contained in the iterator (i.e., the result of de-referencing the iterator)
  // and returns a value that can be fed to to std::hash.
  Callable callable;
};

template <typename Iterator, typename Callable>
auto MakeHashableIteratorRange(Iterator begin, Iterator end,
                               Callable callable) {
  return HashableIteratorRange<Iterator, Callable>{
      .begin = std::move(begin),
      .end = std::move(end),
      .callable = std::move(callable)};
}

template <typename Iterator>
auto MakeHashableIteratorRange(Iterator begin, Iterator end) {
  return MakeHashableIteratorRange(std::move(begin), std::move(end),
                                   [](auto value) { return value; });
}

template <typename Container>
auto MakeHashableIteratorRange(Container container) {
  return MakeHashableIteratorRange(std::begin(container), std::end(container));
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
  explicit HashableContainer(Container input_container)
      : container(std::move(input_container)) {}
  HashableContainer() = default;
  Container container;
};

template <typename T>
struct WithHash {
  size_t hash;
  const T value;
};

template <typename T>
WithHash<T> MakeWithHash(T value, size_t hash) {
  return WithHash<T>{.hash = hash, .value = value};
}
}  // namespace afc::language
namespace std {
template <typename Iterator, typename Callable>
struct hash<afc::language::HashableIteratorRange<Iterator, Callable>> {
  std::size_t operator()(
      const afc::language::HashableIteratorRange<Iterator, Callable>& range) {
    size_t hash_value = 0;
    for (auto it = range.begin; it != range.end; ++it) {
      using Element = typename std::remove_const<typename std::remove_reference<
          decltype(range.callable(*it))>::type>::type;
      hash_value = afc::language::hash_combine(
          hash_value, std::hash<Element>{}(range.callable(*it)));
    }
    return hash_value;
  };
};

template <typename Container>
struct hash<afc::language::HashableContainer<Container>> {
  std::size_t operator()(
      const afc::language::HashableContainer<Container>& container) const {
    return afc::language::compute_hash(afc::language::MakeHashableIteratorRange(
        container.container.begin(), container.container.end()));
  }
};

template <typename T>
struct hash<afc::language::WithHash<T>> {
  std::size_t operator()(const afc::language::WithHash<T>& with_hash) const {
    return with_hash.hash;
  }
};
}  // namespace std

#endif  // __AFC_EDITOR_SRC_HASH_H__
