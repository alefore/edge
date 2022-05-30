// Defines a `container_traits` templated type with a few specializations.
#ifndef __AFC_EDITOR_CONTAINER_TRAITS_H__
#define __AFC_EDITOR_CONTAINER_TRAITS_H__

namespace std {
template <typename T, typename A>
class vector;

template <typename Key, typename Hash, typename KeyEqual, typename Allocator>
class unordered_set;

template <typename Key, typename Value, typename Hash, typename KeyEqual,
          typename Allocator>
class unordered_map;
};  // namespace std

namespace afc::language {
template <typename T>
struct container_traits {};

template <typename T>
struct container_traits<std::vector<T>> {
  static constexpr auto push_back = [](auto& t) -> auto& { return t; };
  static constexpr auto pop_back = [](auto& t) -> auto& { return t; };
};

template <typename K, typename H, typename KE, typename A>
struct container_traits<std::unordered_set<K, H, KE, A>> {};

template <typename K, typename V, typename H, typename KE, typename A>
struct container_traits<std::unordered_map<K, V, H, KE, A>> {};

}  // namespace afc::language

#endif  // __AFC_EDITOR_CONTAINER_TRAITS_H__
