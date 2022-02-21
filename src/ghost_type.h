// Macros for easily defining ghost types.
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
//
// For example:
//
//   namespace xxx {
//   class FirstName {
//    public:
//     GHOST_TYPE_CONSTRUCTOR(FirstName, value);
//     GHOST_TYPE_EQ(FirstName, value);
//     GHOST_TYPE_LT(FirstName, value);
//
//    private:
//     GHOST_TYPE_OUTPUT_FRIEND(FirstName, value);
//     GHOST_TYPE_HASH_FRIEND(FirstName, value);
//     std::string value;
//   };
//
//   GHOST_TYPE_OUTPUT(FirstName, value);
//   }  // namespace xxx
//   GHOST_TYPE_HASH(xxx::FirstName, value);
//
// This is enough to make the following expressions valid:
//
//   LastName last_name_foo("Forero Cuervo");
//
//   if (last_name_a == last_name_b) return;
//
//   std::vector<LastName> names;
//   sort(names.begin(), names.end());
//
//   std::out << "Found name: " << last_name_foo;
#ifndef __AFC_EDITOR_GHOST_TYPE_H__
#define __AFC_EDITOR_GHOST_TYPE_H__

#include <functional>

namespace afc::editor {
#define GHOST_TYPE_CONSTRUCTOR(ClassName, variable) \
  template <typename VariableType>                  \
  explicit ClassName(VariableType variable) : variable(std::move(variable)) {}

#define GHOST_TYPE_CONSTRUCTOR_EMPTY(ClassName) ClassName() = default;

#define GHOST_TYPE_EQ(ClassName, variable)        \
  bool operator==(const ClassName& other) const { \
    return variable == other.variable;            \
  }                                               \
  bool operator!=(const ClassName& other) const { return !(*this == other); }

#define GHOST_TYPE_LT(ClassName, variable)       \
  bool operator<(const ClassName& other) const { \
    return variable < other.variable;            \
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
#define GHOST_TYPE_HASH(ClassName, variable)              \
  template <>                                             \
  struct std::hash<ClassName> {                           \
    std::size_t operator()(const ClassName& self) const { \
      return std::hash<std::wstring>{}(self.variable);    \
    }                                                     \
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
    os << "[" #ClassName ":" << obj.variable << "]";                        \
    return os;                                                              \
  }

}  // namespace afc::editor

#endif  //__AFC_EDITOR_GHOST_TYPE_H__
