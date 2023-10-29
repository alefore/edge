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

template <typename Predicate, typename Value>
void EraseIf(std::vector<Value>& container, Predicate predicate) {
  container.erase(
      std::remove_if(container.begin(), container.end(), std::move(predicate)),
      container.end());
}

// Prefer `EraseIf` over direct calls to the container's remove_if method. That
// allows us to mostly avoid `remove_if`, which, despite its name, in the case
// of vectors doesn't actually remove any elements, just reorders them.
template <typename Predicate, typename Value>
void EraseIf(std::list<Value>& container, Predicate predicate) {
  container.remove_if(std::move(predicate));
}
}  // namespace afc::language
#endif