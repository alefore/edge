#include "src/vm/public/environment.h"

#include <glog/logging.h>

#include <map>
#include <set>

#include "numbers.h"
#include "src/vm/internal/string.h"
#include "src/vm/internal/time.h"
#include "src/vm/internal/types_promotion.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/set.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
namespace gc = language::gc;

template <>
const VMType VMTypeMapper<std::vector<int>*>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"VectorInt"));

template <>
const VMType VMTypeMapper<std::set<int>*>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"SetInt"));
}  // namespace

language::gc::Root<Environment> Environment::NewDefault(
    language::gc::Pool& pool) {
  gc::Root<Environment> environment =
      pool.NewRoot(MakeNonNullUnique<Environment>());
  Environment& value = environment.ptr().value();
  RegisterStringType(pool, value);
  RegisterNumberFunctions(pool, value);
  RegisterTimeType(pool, value);
  auto bool_type = MakeNonNullUnique<ObjectType>(VMType::Bool());
  bool_type->AddField(
      L"tostring", NewCallback(pool, std::function<wstring(bool)>([](bool v) {
                                 return v ? L"true" : L"false";
                               }),
                               VMType::PurityType::kPure));
  value.DefineType(std::move(bool_type));

  auto int_type = MakeNonNullUnique<ObjectType>(VMType::Int());
  int_type->AddField(
      L"tostring",
      NewCallback(pool, std::function<std::wstring(int)>([](int value) {
                    return std::to_wstring(value);
                  }),
                  VMType::PurityType::kPure));
  value.DefineType(std::move(int_type));

  auto double_type = MakeNonNullUnique<ObjectType>(VMType::Double());
  double_type->AddField(
      L"tostring",
      NewCallback(pool, std::function<std::wstring(double)>([](double value) {
                    return std::to_wstring(value);
                  }),
                  VMType::PurityType::kPure));
  double_type->AddField(
      L"round", NewCallback(pool, std::function<int(double)>([](double value) {
                              return static_cast<int>(value);
                            }),
                            VMType::PurityType::kPure));
  value.DefineType(std::move(double_type));

  VMTypeMapper<std::vector<int>*>::Export(pool, value);
  VMTypeMapper<std::set<int>*>::Export(pool, value);
  return environment;
}

void Environment::Clear() {
  object_types_.clear();
  table_.clear();
}

std::optional<language::gc::Ptr<Environment>> Environment::parent_environment()
    const {
  return parent_environment_;
}

const ObjectType* Environment::LookupObjectType(const wstring& symbol) {
  // TODO(easy, 2022-05-17): Receive the symbol directly.
  const VMTypeObjectTypeName name(symbol);
  if (auto it = object_types_.find(name); it != object_types_.end()) {
    return it->second.get().get();
  }
  if (parent_environment_.has_value()) {
    return (*parent_environment_)->LookupObjectType(symbol);
  }
  return nullptr;
}

const VMType* Environment::LookupType(const wstring& symbol) {
  if (symbol == L"void") {
    return &VMType::Void();
  } else if (symbol == L"bool") {
    return &VMType::Bool();
  } else if (symbol == L"int") {
    return &VMType::Int();
  } else if (symbol == L"string") {
    return &VMType::String();
  }

  auto object_type = LookupObjectType(symbol);
  return object_type == nullptr ? nullptr : &object_type->type();
}

Environment::Environment() = default;

Environment::Environment(std::optional<gc::Ptr<Environment>> parent_environment)
    : parent_environment_(std::move(parent_environment)) {}

/* static */ gc::Root<Environment> Environment::NewNamespace(
    gc::Pool& pool, gc::Root<Environment> parent, std::wstring name) {
  if (std::optional<gc::Root<Environment>> previous =
          LookupNamespace(parent, {name});
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

void Environment::DefineType(
    language::NonNull<std::unique_ptr<ObjectType>> value) {
  VMTypeObjectTypeName name = value->type().object_type;
  object_types_.insert_or_assign(name, std::move(value));
}

std::optional<gc::Root<Value>> Environment::Lookup(
    gc::Pool& pool, const Namespace& symbol_namespace, const wstring& symbol,
    VMType expected_type) {
  std::vector<gc::Root<Value>> values;
  PolyLookup(symbol_namespace, symbol, &values);
  for (gc::Root<Value>& value : values) {
    if (auto callback = GetImplicitPromotion(value.ptr()->type, expected_type);
        callback != nullptr) {
      return std::move(callback(
          pool, pool.NewRoot(MakeNonNullUnique<Value>(value.ptr().value()))));
    }
  }
  return std::nullopt;
}

void Environment::PolyLookup(const wstring& symbol,
                             std::vector<gc::Root<Value>>* output) {
  static const auto* empty_namespace = new Environment::Namespace();
  PolyLookup(*empty_namespace, symbol, output);
}

void Environment::PolyLookup(const Environment::Namespace& symbol_namespace,
                             const wstring& symbol,
                             std::vector<gc::Root<Value>>* output) {
  Environment* environment = this;
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
    const Environment::Namespace& symbol_namespace, const wstring& symbol,
    std::vector<gc::Root<Value>>* output) {
  Environment* environment = this;
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
  VMType type = value.ptr()->type;
  table_[symbol].insert_or_assign(type, std::move(value.ptr()));
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
}

void Environment::Remove(const wstring& symbol, VMType type) {
  auto it = table_.find(symbol);
  if (it == table_.end()) return;
  it->second.erase(type);
}

void Environment::ForEachType(
    std::function<void(const std::wstring&, ObjectType&)> callback) {
  if (parent_environment_.has_value()) {
    (*parent_environment_)->ForEachType(callback);
  }
  for (const std::pair<const VMTypeObjectTypeName,
                       NonNull<std::unique_ptr<ObjectType>>>& entry :
       object_types_) {
    // TODO(easy, 2022-05-17): Get rid of the `read`:
    callback(entry.first.read(), entry.second.value());
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
    for (const std::pair<const VMType, gc::Ptr<Value>>& type_entry :
         symbol_entry.second) {
      callback(symbol_entry.first, type_entry.second);
    }
  }
}

std::vector<language::NonNull<std::shared_ptr<gc::ControlFrame>>>
Environment::Expand() const {
  std::vector<language::NonNull<std::shared_ptr<gc::ControlFrame>>> output;
  if (parent_environment().has_value()) {
    output.push_back(parent_environment()->control_frame());
  }
  ForEachNonRecursive(
      [&output](const std::wstring&, const gc::Ptr<Value>& value) {
        output.push_back(value.control_frame());
      });
  for (std::pair<std::wstring, gc::Ptr<Environment>> entry : namespaces_) {
    output.push_back(entry.second.control_frame());
  }
  return output;
}

}  // namespace afc::vm
namespace afc::language::gc {
std::vector<language::NonNull<std::shared_ptr<ControlFrame>>> Expand(
    const afc::vm::Environment& environment) {
  return environment.Expand();
}
}  // namespace afc::language::gc
