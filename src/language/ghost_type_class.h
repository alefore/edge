#ifndef __AFC_EDITOR_GHOST_TYPE_CLASS_H__
#define __AFC_EDITOR_GHOST_TYPE_CLASS_H__

#include <glog/logging.h>

#include <variant>

#include "src/language/wstring.h"

// Declarations redundant with symbols from src/language/error/value_or_error.h.
// We provide them here so that we don't have to depend on that header, allowing
// that header to depend on this one.
namespace afc::language {
class Error;

template <typename T>
using ValueOrError = std::variant<T, Error>;

template <typename T>
bool IsError(const T&);
}  // namespace afc::language

namespace afc::language {
namespace ghost_type_internal {
class AlwaysValid {};

template <typename T>
concept IsAlwaysValid = std::is_same_v<T, AlwaysValid>;

template <typename External>
class Factory {
 public:
  static ValueOrError<External> New(typename External::InternalType value) {
    if (auto possible_error = External::ValidatorType::Validate(value);
        std::holds_alternative<Error>(possible_error))
      return std::get<Error>(possible_error);
    return External(value);
  };
};

// Specialization for AlwaysValid type:
template <typename External>
  requires std::is_same_v<typename External::ValidatorType, AlwaysValid>
class Factory<External> {
 public:
  static External New(External::InternalType value) { return External(value); };
};

template <typename Internal>
concept HasValueType = requires { typename Internal::value_type; };

template <typename Internal, bool = HasValueType<Internal>>
struct ValueType {};

// Specialization of GhostTypeValueType when Internal has value_type
template <typename Internal>
struct ValueType<Internal, true> {
  using value_type = typename Internal::value_type;
};

template <typename Internal>
concept HasSize = requires(Internal i) { i.size(); };

template <typename Internal>
concept HasToBytes = requires(Internal i) {
  { i.ToBytes() } -> std::same_as<std::string>;
};

template <typename Internal>
concept HasEmpty = requires(Internal i) {
  { i.empty() } -> std::same_as<bool>;
};

template <typename Internal, typename Key>
concept HasFind = requires(Internal i, Key key) {
  { i.find(key) } -> std::same_as<typename Internal::iterator>;
};

template <typename Internal, typename ElementValue>
concept HasInsert = requires(Internal container, ElementValue element) {
  {
    container.insert(element)
  } -> std::same_as<std::pair<typename Internal::iterator, bool>>;
} || requires(Internal container, ElementValue element) {
  { container.insert(element) } -> std::same_as<typename Internal::iterator>;
};

template <typename Internal>
concept HasBegin = requires(Internal i) {
  { i.begin() } -> std::same_as<typename Internal::iterator>;
};

template <typename Internal, typename ElementValue>
concept HasPushBack =
    requires(Internal i, ElementValue element) { i.push_back(element); };

template <typename Internal>
concept HasPopBack = requires(Internal i) { i.pop_back(); };

template <typename Internal>
concept HasEnd = requires(Internal i) {
  { i.end() } -> std::same_as<typename Internal::iterator>;
};

template <typename Internal, typename Key>
concept HasSubscriptOperator = requires(Internal i, Key key) {
  { i[key] };
};
}  // namespace ghost_type_internal

template <typename External, typename Internal, typename Validator>
class GhostType;
}  // namespace afc::language
namespace std {
template <typename External, typename Internal, typename Validator>
struct hash<afc::language::GhostType<External, Internal, Validator>>;
}
namespace afc::language {

template <typename T, typename = void>
struct IsGhostType : std::false_type {};

template <typename T>
struct IsGhostType<T, std::void_t<typename std::enable_if<std::is_base_of_v<
                          afc::language::GhostType<T, typename T::InternalType,
                                                   typename T::ValidatorType>,
                          T>>::type>> : std::true_type {};

template <typename External, typename Internal, typename Validator>
auto operator+(const GhostType<External, Internal, Validator>& lhs,
               const GhostType<External, Internal, Validator>& rhs);

template <typename External, typename Internal, typename Validator>
auto operator+(const GhostType<External, Internal, Validator>& lhs,
               const Internal& t);

template <typename External, typename Internal, typename Validator>
auto operator+(const Internal& t,
               const GhostType<External, Internal, Validator>& rhs);

template <typename External, typename Internal, typename Validator>
auto operator*(const GhostType<External, Internal, Validator>& lhs,
               const GhostType<External, Internal, Validator>& rhs);

template <typename External, typename Internal, typename Validator>
auto operator*(const GhostType<External, Internal, Validator>& lhs,
               const Internal& t);

template <typename External, typename Internal, typename Validator>
auto operator*(const Internal& t,
               const GhostType<External, Internal, Validator>& rhs);

template <typename External, typename Internal, typename Validator>
inline std::ostream& operator<<(
    std::ostream& os, const GhostType<External, Internal, Validator>& obj);

template <typename External, typename Internal, typename Validator>
inline std::wstring to_wstring(
    const GhostType<External, Internal, Validator>& obj);

template <typename External, typename Internal,
          typename Validator = ghost_type_internal::AlwaysValid>
class GhostType : public ghost_type_internal::ValueType<Internal> {
  Internal value;

 public:
  using InternalType = Internal;
  using ValidatorType = Validator;

  GhostType() = default;
  GhostType(Internal initial_value) : value(std::move(initial_value)) {
    if constexpr (!ghost_type_internal::IsAlwaysValid<Validator>) {
      auto result = External::ValidatorType::Validate(value);
      CHECK(!IsError(result)) << std::get<Error>(result);
    }
  }
  GhostType(const GhostType&) = default;

  template <typename T>
  GhostType(T&& initial_value)
    requires(
        std::is_constructible_v<Internal, T> &&
        !std::is_same_v<std::remove_cvref_t<T>, GhostType> &&
        !std::is_same_v<std::remove_cvref_t<T>,
                        std::initializer_list<typename Internal::value_type>>)
      : GhostType(Internal(std::forward<T>(initial_value))) {}

  template <typename T>
  GhostType(std::initializer_list<T> init_list)
    requires std::is_constructible_v<Internal, std::initializer_list<T>>
      : GhostType(Internal(init_list)) {}

  template <typename K, typename V>
  GhostType(std::initializer_list<std::pair<const K, V>> init_list)
    requires std::is_constructible_v<
        Internal, std::initializer_list<std::pair<const K, V>>>
      : GhostType(Internal(init_list)) {}

  static auto New(Internal internal) {
    return ghost_type_internal::Factory<External>::New(internal);
  }

  auto size() const
    requires ghost_type_internal::HasSize<Internal>
  {
    return value.size();
  }

  auto empty() const
    requires ghost_type_internal::HasEmpty<Internal>
  {
    return value.empty();
  }

  std::string ToBytes() const
    requires ghost_type_internal::HasToBytes<Internal>
  {
    return value.ToBytes();
  }

  template <typename Key>
  auto find(const Key& key) const
    requires ghost_type_internal::HasFind<Internal, Key>
  {
    return value.find(key);
  }

  template <typename Value>
  auto insert(const Value& element)
    requires ghost_type_internal::HasInsert<Internal, Value>
  {
    return value.insert(element);
  }

  template <typename Value>
  auto push_back(const Value& element)
    requires ghost_type_internal::HasPushBack<Internal, Value>
  {
    return value.push_back(element);
  }

  auto pop_back()
    requires ghost_type_internal::HasPopBack<Internal>
  {
    return value.pop_back();
  }

  auto begin() const
    requires ghost_type_internal::HasBegin<Internal>
  {
    return value.begin();
  }

  auto end() const
    requires ghost_type_internal::HasEnd<Internal>
  {
    return value.end();
  }

  template <typename Key>
  auto& operator[](const Key& key)
    requires ghost_type_internal::HasSubscriptOperator<Internal, Key>
  {
    return value[key];
  }

  template <typename T = External>
  bool operator<(const T& other) const {
    return value < other.value;
  }

  template <typename T>
  auto operator/(const T& other) const {
    return External::New(value / other);
  }

  inline External& operator*=(double double_value) {
    value *= double_value;
    return *static_cast<External*>(this);
  }

  inline External& operator*=(
      const GhostType<External, Internal, Validator>& other) {
    value *= other.value;
    return *static_cast<External*>(this);
  }

  inline bool operator==(
      const GhostType<External, Internal, Validator>& other) const {
    return value == other.value;
  }

  inline bool operator>=(
      const GhostType<External, Internal, Validator>& other) const {
    return value >= other.value;
  }

  const Internal& read() const& { return value; }

  Internal read() && { return std::move(value); }

 private:
  friend auto operator+ <External, Internal, Validator>(
      const GhostType<External, Internal, Validator>&,
      const GhostType<External, Internal, Validator>&);

  friend auto operator+ <External, Internal, Validator>(
      const GhostType<External, Internal, Validator>&, const Internal&);

  friend auto operator+ <External, Internal, Validator>(
      const Internal&, const GhostType<External, Internal, Validator>&);

  friend auto operator* <External, Internal, Validator>(
      const GhostType<External, Internal, Validator>&,
      const GhostType<External, Internal, Validator>&);

  friend auto operator* <External, Internal, Validator>(
      const GhostType<External, Internal, Validator>&, const Internal&);

  friend auto operator* <External, Internal, Validator>(
      const Internal&, const GhostType<External, Internal, Validator>&);

  friend std::ostream& operator<< <External, Internal, Validator>(
      std::ostream& os, const GhostType<External, Internal, Validator>& obj);

  friend struct std::hash<GhostType<External, Internal, Validator>>;

  friend std::wstring to_wstring<External, Internal, Validator>(
      const GhostType<External, Internal, Validator>& obj);
};

template <typename External, typename Internal, typename Validator>
auto operator+(const GhostType<External, Internal, Validator>& lhs,
               const GhostType<External, Internal, Validator>& rhs) {
  return External::New(lhs.value + rhs.value);
}

template <typename External, typename Internal, typename Validator>
auto operator+(const Internal& t,
               const GhostType<External, Internal, Validator>& rhs) {
  return External::New(t + rhs.value);
}

template <typename External, typename Internal, typename Validator>
auto operator+(const GhostType<External, Internal, Validator>& lhs,
               const Internal& t) {
  return External::New(lhs.value + t);
}

template <typename External, typename Internal, typename Validator>
auto operator*(const GhostType<External, Internal, Validator>& lhs,
               const GhostType<External, Internal, Validator>& rhs) {
  return External::New(lhs.value * rhs.value);
}

template <typename External, typename Internal, typename Validator>
auto operator*(const Internal& t,
               const GhostType<External, Internal, Validator>& rhs) {
  return External::New(t * rhs.value);
}

template <typename External, typename Internal, typename Validator>
auto operator*(const GhostType<External, Internal, Validator>& lhs,
               const Internal& t) {
  return External::New(lhs.value * t);
}

template <typename External, typename Internal, typename Validator>
inline std::ostream& operator<<(
    std::ostream& os, const GhostType<External, Internal, Validator>& obj) {
  using ::operator<<;
  os << "[" /*name*/ ":" << obj.value << "]";
  return os;
}

std::wstring to_wstring(std::wstring s);

template <typename External, typename Internal, typename Validator>
inline std::wstring to_wstring(
    const GhostType<External, Internal, Validator>& obj) {
  using afc::language::to_wstring;
  using std::to_wstring;
  return to_wstring(obj.value);
}
}  // namespace afc::language
namespace std {
template <typename External, typename Internal, typename Validator>
struct hash<afc::language::GhostType<External, Internal, Validator>> {
  std::size_t operator()(
      const afc::language::GhostType<External, Internal, Validator>& self)
      const noexcept {
    return std::hash<Internal>()(self.value);
  }
};

template <typename T>
  requires ::afc::language::IsGhostType<T>::value
struct hash<T> {
  std::size_t operator()(const T& self) const noexcept {
    using BaseType = ::afc::language::GhostType<T, typename T::InternalType,
                                                typename T::ValidatorType>;
    return std::hash<BaseType>()(self);
  }
};
}  // namespace std
#endif
