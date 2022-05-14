// Macros for easily defining ghost types:
//
//   namespace foo::bar {
//     GHOST_TYPE(HistoryFile, std::wstring)
//   }
//   GHOST_TYPE_TOP_LEVEL(foo::bar::HistoryFile);
//
// This is based on the principle that code is more readable if the types it
// operates on convey more semantics than just what their underlying
// representation as basic types is (e.g., string, int, etc.).
//
// For example, suppose you have a class that represents the following values as
// strings:
//
// - First name
// - Last name
// - Email
//
// Instead of writing expressions such as:
//
//   string my_first_name;
//   string my_last_name;
//   Person New(string first_name, string last_name, string email);
//   string GetEmail(const Person& person);
//
// We think it's better to use "alias" types (in this case FirstName, LastName
// and Email):
//
//   FirstName my_first_name;
//   LastName my_last_name;
//   Person New(FirstName first_name, LastName last_name, Email email);
//   Email GetEmail(const Person& person);
//
// Of course, in C++ one could simply do:
//
//   using FirstName = string;
//   using LastName = string;
//   using Email = string;
//
// Unfortunately, with this approach, C++ won't detect typing errors if the
// types are used incorrectly, such as in:
//
//   FirstName foo = GetEmail(my_person);
//
// This module proposes that you define "ghost types": a simple structure that
// contains a single field with the underlying data representation, such as:
//
//   class FirstName {
//    public:
//     FirstName(string value) : value(value) {}
//    private:
//     string value;
//   }
//   class LastName { ... }
//   class Email { ... }
//
// The macros provided here enable you to automatically declare various
// desirable operators for such types, so that they can, for example, be
// directly compared (based on the operators for the underlying
// representations).
//
// You may sometimes need to provide accessors exposing the underlying
// representation, like:
//
//   class FirstName {
//    public:
//     ...
//     string get() const { return value; }
//     void set(string update) { value = update; }
//     ...
//   };
//
// However, the methods provided here may be enough that you won't even need to
// expose the internal type in some cases.
#ifndef __AFC_EDITOR_GHOST_TYPE_H__
#define __AFC_EDITOR_GHOST_TYPE_H__

#include <functional>

#define GHOST_TYPE(ClassName, VariableType)                 \
  class ClassName {                                         \
   public:                                                  \
    GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, value); \
    GHOST_TYPE_EQ(ClassName, value);                        \
    GHOST_TYPE_ORDER(ClassName, value);                     \
                                                            \
    const VariableType& read() const { return value; }      \
                                                            \
   private:                                                 \
    GHOST_TYPE_OUTPUT_FRIEND(ClassName, value);             \
    GHOST_TYPE_HASH_FRIEND(ClassName, value);               \
    VariableType value;                                     \
  };                                                        \
                                                            \
  GHOST_TYPE_OUTPUT(ClassName, value);

#define GHOST_TYPE_TOP_LEVEL(ClassName) GHOST_TYPE_HASH(ClassName);

#define GHOST_TYPE_NUMBER(ClassName, VariableType)          \
  class ClassName {                                         \
   public:                                                  \
    GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, value); \
    GHOST_TYPE_EQ(ClassName, value);                        \
    GHOST_TYPE_ORDER(ClassName, value)                      \
                                                            \
    const auto& read() const { return value; }              \
                                                            \
   private:                                                 \
    GHOST_TYPE_OUTPUT_FRIEND(ClassName, value);             \
    GHOST_TYPE_HASH_FRIEND(ClassName, value);               \
    VariableType value;                                     \
  };                                                        \
                                                            \
  GHOST_TYPE_OUTPUT(ClassName, value);

#define GHOST_TYPE_DOUBLE(ClassName)   \
  GHOST_TYPE_NUMBER(ClassName, double) \
  GHOST_TYPE_NUMBER_OPERATORS(ClassName, double);

#define GHOST_TYPE_SIZE_T(ClassName)   \
  GHOST_TYPE_NUMBER(ClassName, size_t) \
  GHOST_TYPE_NUMBER_OPERATORS(ClassName, size_t);

#define GHOST_TYPE_CONTAINER(ClassName, VariableType)       \
  class ClassName {                                         \
   public:                                                  \
    GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, value); \
    GHOST_TYPE_EQ(ClassName, value);                        \
    GHOST_TYPE_BEGIN_END(ClassName, value);                 \
    GHOST_TYPE_INDEX(ClassName, value);                     \
    const VariableType& read() const { return value; }      \
                                                            \
   private:                                                 \
    VariableType value;                                     \
  };

// Implementation:

#define GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, variable) \
  explicit ClassName(VariableType variable) : variable(std::move(variable)) {}

#define GHOST_TYPE_CONSTRUCTOR_EMPTY(ClassName) ClassName() = default;

#define GHOST_TYPE_NUMBER_OPERATORS(ClassName, VariableType)           \
  inline ClassName operator+(const ClassName& a, VariableType other) { \
    return ClassName(a.read() + other);                                \
  }                                                                    \
                                                                       \
  inline ClassName operator+(VariableType other, const ClassName& a) { \
    return a + other;                                                  \
  }                                                                    \
                                                                       \
  inline ClassName operator+(const ClassName& a, const ClassName& b) { \
    return ClassName(a.read() + b.read());                             \
  }                                                                    \
                                                                       \
  inline ClassName operator-(const ClassName& a, VariableType other) { \
    return ClassName(a.read() - other);                                \
  }                                                                    \
                                                                       \
  inline ClassName operator-(VariableType other, const ClassName& a) { \
    return a - other;                                                  \
  }                                                                    \
                                                                       \
  inline ClassName operator-(const ClassName& a, const ClassName& b) { \
    return ClassName(a.read() - b.read());                             \
  }                                                                    \
                                                                       \
  inline ClassName operator*(const ClassName& a, VariableType other) { \
    return ClassName(a.read() * other);                                \
  }                                                                    \
                                                                       \
  inline ClassName operator*(VariableType other, const ClassName& a) { \
    return a * other;                                                  \
  }                                                                    \
                                                                       \
  inline ClassName operator*(const ClassName& a, const ClassName& b) { \
    return ClassName(a.read() * b.read());                             \
  }                                                                    \
                                                                       \
  inline ClassName operator/(const ClassName& a, VariableType other) { \
    return ClassName(a.read() / other);                                \
  }                                                                    \
                                                                       \
  inline double operator/(double other, const ClassName& a) {          \
    return other / a.read();                                           \
  }

#define GHOST_TYPE_EQ(ClassName, variable)        \
  bool operator==(const ClassName& other) const { \
    return variable == other.variable;            \
  }                                               \
  bool operator!=(const ClassName& other) const { return !(*this == other); }

#define GHOST_TYPE_ORDER(ClassName, variable)     \
  bool operator<(const ClassName& other) const {  \
    return variable < other.variable;             \
  }                                               \
  bool operator<=(const ClassName& other) const { \
    return variable <= other.variable;            \
  }                                               \
  bool operator>(const ClassName& other) const {  \
    return variable > other.variable;             \
  }                                               \
  bool operator>=(const ClassName& other) const { \
    return variable >= other.variable;            \
  }

// GHOST_TYPE_BEGIN_END declares `begin` and `end` methods for the ghost type.
// This is useful when the underlying representation has corresponding methods:
//
//   class FeaturesSet {
//    public:
//     ...
//     GHOST_TYPE_BEGIN_END(FeaturesSet, features_);
//
//    private:
//     std::unordered_set<Feature> features_;
//   };
//
// This is enough to make this work:
//
//   FeaturesSet features_set;
//   for (const Feature& feature : features_set) ...
#define GHOST_TYPE_BEGIN_END(ClassName, variable) \
  auto begin() const { return variable.begin(); } \
  auto end() const { return variable.end(); }

#define GHOST_TYPE_HASH_FRIEND(ClassName, variable) \
  friend class std::hash<ClassName>;

// Can't be inside of a namespace; must be at the top-level.
#define GHOST_TYPE_HASH(ClassName)                                      \
  template <>                                                           \
  struct std::hash<ClassName> {                                         \
    std::size_t operator()(const ClassName& self) const {               \
      return std::hash<std::remove_const<                               \
          std::remove_reference<decltype(self.read())>::type>::type>()( \
          self.read());                                                 \
    }                                                                   \
  };

#define GHOST_TYPE_INDEX(ClassName, variable)       \
  template <typename KeyType>                       \
  auto& operator[](const KeyType& ghost_type_key) { \
    return variable[ghost_type_key];                \
  }

#define GHOST_TYPE_OUTPUT_FRIEND(ClassName, variable) \
  friend std::ostream& operator<<(std::ostream& os, const ClassName& obj)

#define GHOST_TYPE_OUTPUT(ClassName, variable)                              \
  inline std::ostream& operator<<(std::ostream& os, const ClassName& obj) { \
    using ::operator<<;                                                     \
    os << "[" #ClassName ":" << obj.variable << "]";                        \
    return os;                                                              \
  }

#endif  //__AFC_EDITOR_GHOST_TYPE_H__
