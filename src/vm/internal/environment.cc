#include "src/vm/public/environment.h"

#include <glog/logging.h>

#include <map>
#include <set>

#include "src/language/numbers.h"
#include "src/vm/internal/numbers.h"
#include "src/vm/internal/string.h"
#include "src/vm/internal/time.h"
#include "src/vm/internal/types_promotion.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/container.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::numbers::Number;
using afc::language::numbers::ToString;

namespace afc::vm {

template <>
const types::ObjectName
    VMTypeMapper<NonNull<std::shared_ptr<std::vector<int>>>>::object_type_name =
        types::ObjectName(L"VectorInt");

template <>
const types::ObjectName
    VMTypeMapper<NonNull<std::shared_ptr<std::set<int>>>>::object_type_name =
        types::ObjectName(L"SetInt");

language::gc::Root<Environment> Environment::NewDefault(
    language::gc::Pool& pool) {
  gc::Root<Environment> environment =
      pool.NewRoot(MakeNonNullUnique<Environment>());
  Environment& environment_value = environment.ptr().value();
  RegisterStringType(pool, environment_value);
  RegisterNumberFunctions(pool, environment_value);
  RegisterTimeType(pool, environment_value);
  gc::Root<ObjectType> bool_type = ObjectType::New(pool, types::Bool{});
  bool_type.ptr()->AddField(
      L"tostring", NewCallback(pool, PurityType::kPure,
                               std::function<wstring(bool)>([](bool v) {
                                 return v ? L"true" : L"false";
                               }))
                       .ptr());
  environment_value.DefineType(bool_type.ptr());

  gc::Root<ObjectType> number_type = ObjectType::New(pool, types::Number{});
  number_type.ptr()->AddField(
      L"tostring",
      NewCallback(pool, PurityType::kPure,
                  /*std::function<std::wstring(Number)>*/ ([](Number value) {
                    return futures::Past(ToString(value, 5));
                  }))
          .ptr());
  environment_value.DefineType(number_type.ptr());

#if 0
  double_type.ptr()->AddField(
      L"round", NewCallback(pool, PurityType::kPure,
                            std::function<int(double)>([](double value) {
                              return static_cast<int>(value);
                            }))
                    .ptr());
  environment_value.DefineType(double_type.ptr());
#endif

  environment_value.Define(
      L"Error",
      NewCallback(pool, PurityType::kPure, [](std::wstring description) {
        return futures::Past(PossibleError(Error(description)));
      }));

  container::Export<std::vector<int>>(pool, environment_value);
  container::Export<std::set<int>>(pool, environment_value);
  return environment;
}

// TODO(easy, 2022-12-03): Get rid of this? Now that we have GC, shouldn't be
// needed.
void Environment::Clear() {
  object_types_.clear();
  table_.clear();
}

std::optional<language::gc::Ptr<Environment>> Environment::parent_environment()
    const {
  return parent_environment_;
}

const ObjectType* Environment::LookupObjectType(const types::ObjectName& name) {
  if (auto it = object_types_.find(name); it != object_types_.end()) {
    return &it->second.value();
  }
  if (parent_environment_.has_value()) {
    return (*parent_environment_)->LookupObjectType(name);
  }
  return nullptr;
}

const Type* Environment::LookupType(const wstring& symbol) {
  if (symbol == L"void") {
    static Type output = types::Void{};
    return &output;
  } else if (symbol == L"bool") {
    static Type output = types::Bool{};
    return &output;
  } else if (symbol == L"number") {
    static Type output = types::Number{};
    return &output;
  } else if (symbol == L"string") {
    static Type output = types::String{};
    return &output;
  }

  auto object_type = LookupObjectType(types::ObjectName(symbol));
  return object_type == nullptr ? nullptr : &object_type->type();
}

Environment::Environment() = default;

Environment::Environment(std::optional<gc::Ptr<Environment>> parent_environment)
    : parent_environment_(std::move(parent_environment)) {}

/* static */ gc::Root<Environment> Environment::NewNamespace(
    gc::Pool& pool, gc::Root<Environment> parent, std::wstring name) {
  if (std::optional<gc::Root<Environment>> previous =
          LookupNamespace(parent, Namespace({name}));
      previous.has_value()) {
    return *previous;
  }
  if (auto result = parent.ptr()->namespaces_.find(name);
      result != parent.ptr()->namespaces_.end()) {
    return result->second.ToRoot();
  }

  gc::Root<Environment> namespace_env =
      pool.NewRoot(MakeNonNullUnique<Environment>(parent.ptr()));
  auto [_, inserted] =
      parent.ptr()->namespaces_.insert({name, namespace_env.ptr()});
  CHECK(inserted);
  namespace_env.ptr().Protect();
  return namespace_env;
}

/* static */ std::optional<gc::Root<Environment>> Environment::LookupNamespace(
    gc::Root<Environment> source, const Namespace& name) {
  std::optional<gc::Ptr<Environment>> output = {source.ptr()};
  for (auto& n : name) {
    if (auto it = output.value()->namespaces_.find(n);
        it != output.value()->namespaces_.end()) {
      output = it->second;
      continue;
    }
    output = std::nullopt;
    break;
  }
  if (output.has_value()) {
    return output->ToRoot();
  }
  if (std::optional<gc::Ptr<Environment>> parent_environment =
          source.ptr()->parent_environment();
      parent_environment.has_value()) {
    return LookupNamespace(parent_environment->ToRoot(), name);
  }
  return std::nullopt;
}

void Environment::DefineType(gc::Ptr<ObjectType> value) {
  object_types_.insert_or_assign(NameForType(value->type()), std::move(value));
}

std::optional<gc::Root<Value>> Environment::Lookup(
    gc::Pool& pool, const Namespace& symbol_namespace, const wstring& symbol,
    Type expected_type) {
  std::vector<gc::Root<Value>> values;
  PolyLookup(symbol_namespace, symbol, &values);
  for (gc::Root<Value>& value : values) {
    if (auto callback = GetImplicitPromotion(value.ptr()->type, expected_type);
        callback != nullptr) {
      return callback(
          pool, pool.NewRoot(MakeNonNullUnique<Value>(value.ptr().value())));
    }
  }
  return std::nullopt;
}

void Environment::PolyLookup(const Namespace& symbol_namespace,
                             const wstring& symbol,
                             std::vector<gc::Root<Value>>* output) const {
  const Environment* environment = this;
  for (auto& n : symbol_namespace) {
    CHECK(environment != nullptr);
    auto it = environment->namespaces_.find(n);
    if (it == environment->namespaces_.end()) {
      environment = nullptr;
      break;
    }
    environment = &it->second.value();
  }
  if (environment != nullptr) {
    if (auto it = environment->table_.find(symbol);
        it != environment->table_.end()) {
      for (auto& entry : it->second) {
        output->push_back(entry.second.ToRoot());
      }
    }
  }
  // Deliverately ignoring `environment`:
  if (parent_environment_.has_value()) {
    (*parent_environment_)->PolyLookup(symbol_namespace, symbol, output);
  }
}

void Environment::CaseInsensitiveLookup(
    const Namespace& symbol_namespace, const wstring& symbol,
    std::vector<gc::Root<Value>>* output) const {
  const Environment* environment = this;
  for (auto& n : symbol_namespace) {
    auto it = environment->namespaces_.find(n);
    if (it == environment->namespaces_.end()) {
      environment = nullptr;
      break;
    }
    environment = &it->second.value();
  }
  if (environment != nullptr) {
    for (auto& item : environment->table_) {
      if (wcscasecmp(item.first.c_str(), symbol.c_str()) == 0) {
        for (auto& entry : item.second) {
          output->push_back(entry.second.ToRoot());
        }
      }
    }
  }
  // Deliverately ignoring `environment`:
  if (parent_environment_.has_value()) {
    (*parent_environment_)
        ->CaseInsensitiveLookup(symbol_namespace, symbol, output);
  }
}

void Environment::Define(const wstring& symbol, gc::Root<Value> value) {
  Type type = value.ptr()->type;
  table_[symbol].insert_or_assign(type, value.ptr());
  value.ptr().Protect();
}

void Environment::Assign(const wstring& symbol, gc::Root<Value> value) {
  auto it = table_.find(symbol);
  if (it == table_.end()) {
    // TODO: Show the symbol.
    CHECK(parent_environment_.has_value())
        << "Environment::parent_environment_ is nullptr while trying to "
           "assign a new value to a symbol `...`. This likely means that the "
           "symbol is undefined (which the caller should have validated as "
           "part of the compilation process).";
    (*parent_environment_)->Assign(symbol, std::move(value));
    return;
  }
  it->second.insert_or_assign(value.ptr()->type, value.ptr());
  value.ptr().Protect();
}

void Environment::Remove(const wstring& symbol, Type type) {
  auto it = table_.find(symbol);
  if (it == table_.end()) return;
  it->second.erase(type);
}

void Environment::ForEachType(
    std::function<void(const types::ObjectName&, ObjectType&)> callback) {
  if (parent_environment_.has_value()) {
    (*parent_environment_)->ForEachType(callback);
  }
  for (const std::pair<const types::ObjectName, gc::Ptr<ObjectType>>& entry :
       object_types_) {
    callback(entry.first, entry.second.value());
  }
}

void Environment::ForEach(
    std::function<void(const wstring&, const gc::Ptr<Value>&)> callback) const {
  if (parent_environment_.has_value()) {
    (*parent_environment_)->ForEach(callback);
  }
  ForEachNonRecursive(callback);
}

void Environment::ForEachNonRecursive(
    std::function<void(const std::wstring&, const gc::Ptr<Value>&)> callback)
    const {
  for (const auto& symbol_entry : table_) {
    for (const std::pair<const Type, gc::Ptr<Value>>& type_entry :
         symbol_entry.second) {
      callback(symbol_entry.first, type_entry.second);
    }
  }
}

std::vector<language::NonNull<std::shared_ptr<gc::ObjectMetadata>>>
Environment::Expand() const {
  std::vector<language::NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  if (parent_environment().has_value()) {
    output.push_back(parent_environment()->object_metadata());
  }
  ForEachNonRecursive(
      [&output](const std::wstring&, const gc::Ptr<Value>& value) {
        output.push_back(value.object_metadata());
      });
  for (std::pair<std::wstring, gc::Ptr<Environment>> entry : namespaces_) {
    output.push_back(entry.second.object_metadata());
  }
  for (const std::pair<const types::ObjectName, gc::Ptr<ObjectType>>& entry :
       object_types_) {
    output.push_back(entry.second.object_metadata());
  }
  return output;
}

}  // namespace afc::vm
namespace afc::language::gc {
std::vector<language::NonNull<std::shared_ptr<ObjectMetadata>>> Expand(
    const afc::vm::Environment& environment) {
  return environment.Expand();
}
}  // namespace afc::language::gc
