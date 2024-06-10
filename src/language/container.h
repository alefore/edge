#ifndef __AFC_LANGUAGE_CONTAINERS__
#define __AFC_LANGUAGE_CONTAINERS__

#include <algorithm>
#include <list>
#include <optional>
#include <ranges>
#include <set>
#include <type_traits>
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

template <typename Container, typename KeyType>
const std::optional<typename Container::mapped_type> GetValueOrNullOpt(
    const Container& container, const KeyType& key) {
  if (auto it = container.find(key); it != container.end()) return it->second;
  return std::nullopt;
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

template <typename Predicate, typename Value>
void EraseIf(std::set<Value>& container, Predicate predicate) {
  for (auto it = container.begin(); it != container.end(); /* Skip. */)
    if (predicate(*it))
      it = container.erase(it);  // Erase returns the next valid iterator.
    else
      ++it;
}

template <typename Container>
std::set<typename Container::key_type> GetSetWithKeys(
    const Container& container) {
  return std::set<typename Container::key_type>(
      std::views::keys(container).begin(), std::views::keys(container).end());
}

namespace container {
template <typename Range, typename Predicate>
std::optional<typename Range::value_type> FindFirstIf(Range&& range,
                                                      Predicate pred) {
  if (auto it = std::ranges::find_if(range, pred);
      it != std::ranges::end(range))
    return *it;
  return std::nullopt;
}

// Convenience function. Hopefully we'll be able to do this with native C++
// soon, and then we can just get rid of this.
template <typename Container>
Container Materialize(auto&& view) {
  return Container(view.begin(), view.end());
}

auto MaterializeVector(auto&& view) {
  return Materialize<std::vector<std::decay_t<decltype(*view.begin())>>>(
      std::move(view));
}

auto MaterializeSet(auto&& view) {
  return Materialize<std::set<std::decay_t<decltype(*view.begin())>>>(
      std::move(view));
}

auto MaterializeList(auto&& view) {
  return Materialize<std::list<std::decay_t<decltype(*view.begin())>>>(
      std::move(view));
}

template <typename Container, typename Callable, typename Value>
Value Fold(Callable aggregate, Value identity, Container&& container) {
  Value output = std::move(identity);
  for (auto&& value : container)
    output = aggregate(std::move(value), std::move(output));

  return output;
}

template <typename Container, typename Callable, typename Value>
std::optional<Value> FoldOptional(Callable aggregate, Value identity,
                                  Container&& container) {
  std::optional<Value> output = std::make_optional(std::move(identity));
  for (auto&& value : container)
    if (output.has_value())
      output = aggregate(std::move(value), std::move(output.value()));
  return output;
}

template <typename Container, typename OutputType>
OutputType Sum(OutputType identity, Container&& container) {
  return Fold([](auto input, OutputType output) { return input + output; },
              std::move(identity), std::forward<Container>(container));
}

template <typename Container, typename OutputType = std::decay_t<
                                  decltype(*std::declval<Container>().begin())>>
OutputType Sum(Container&& container) {
  return Fold([](auto input, OutputType output) { return input + output; },
              OutputType(), std::forward<Container>(container));
}
}  // namespace container
}  // namespace afc::language
#endif