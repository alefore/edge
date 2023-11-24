#ifndef __AFC_LANGUAGE_CONTAINERS__
#define __AFC_LANGUAGE_CONTAINERS__

#include <list>
#include <ranges>
#include <set>
#include <vector>

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

template <typename Container>
std::set<typename Container::key_type> GetSetWithKeys(
    const Container& container) {
  return std::set<typename Container::key_type>(
      std::views::keys(container).begin(), std::views::keys(container).end());
}

template <typename Container, typename Callable, typename Value>
Value Fold(Callable aggregate, Value identity, Container&& container) {
  Value output = std::move(identity);
  for (auto&& value : container)
    output = aggregate(std::move(value), std::move(output));

  return output;
}

namespace container {
template <typename InputContainer, typename Callable, typename Container>
auto Map(InputContainer&& input, Callable callable, Container output) {
  output.reserve(input.size());
  for (auto&& value : input) output.push_back(callable(std::move(value)));
  return output;
}

template <typename InputContainer, typename Callable>
auto Map(InputContainer&& input, Callable callable) {
  return Map(std::forward<InputContainer>(input), std::move(callable),
             std::vector<decltype(callable(*input.begin()))>());
}

template <typename Value, typename Callable>
auto Map(const std::vector<Value>& input, Callable callable) {
  std::vector<decltype(callable(std::declval<Value>()))> output;
  output.reserve(input.size());
  for (const auto& value : input) output.push_back(callable(value));
  return output;
}
}  // namespace container
}  // namespace afc::language
#endif