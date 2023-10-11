#ifndef __AFC_LANGUAGE_CONTAINERS__
#define __AFC_LANGUAGE_CONTAINERS__

namespace afc::language {
template <typename Container>
void EraseOrDie(Container& container, const typename Container::key_type& key) {
  CHECK(container.erase(key) == 1);
}

template <typename Container>
auto InsertOrDie(Container& container, typename Container::value_type value) {
  auto [it, success] = container.insert(std::move(value));
  CHECK(success);
  return it;
}

template <typename Container, typename KeyType>
auto FindOrDie(Container& container, const KeyType& key) {
  auto it = container.find(key);
  CHECK(it != container.end());
  return it;
}

template <typename Container, typename KeyType>
const auto& GetValueOrDie(const Container& container, const KeyType& key) {
  return FindOrDie(container, key)->second;
}

template <typename Container>
Container::mapped_type& GetValueOrDie(Container& container,
                                      const typename Container::key_type& key) {
  return FindOrDie(container, key)->second;
}

template <typename Container, typename KeyType, typename Value>
const auto& GetValueOrDefault(const Container& container, const KeyType& key,
                              const Value& default_value) {
  if (auto it = container.find(key); it != container.end()) return it->second;
  return default_value;
}

template <typename Container>
Container::mapped_type PopValueOrDie(Container& container,
                                     const typename Container::key_type& key) {
  auto it = container.find(key);
  CHECK(it != container.end());
  auto output = it->second;
  container.erase(it);
  return output;
}
}  // namespace afc::language
#endif