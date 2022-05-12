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
    VMType::ObjectType(L"VectorInt");

template <>
const VMType VMTypeMapper<std::set<int>*>::vmtype =
    VMType::ObjectType(L"SetInt");
}  // namespace

language::gc::Root<Environment> Environment::NewDefault(
    language::gc::Pool& pool) {
  gc::Root<Environment> environment =
      pool.NewRoot(MakeNonNullUnique<Environment>());
  RegisterStringType(pool, &environment.value().value());
  RegisterNumberFunctions(pool, &environment.value().value());
  RegisterTimeType(pool, &environment.value().value());
  auto bool_type = MakeNonNullUnique<ObjectType>(VMType::Bool());
  bool_type->AddField(
      L"tostring", NewCallback(pool, std::function<wstring(bool)>([](bool v) {
                                 return v ? L"true" : L"false";
                               }),
                               VMType::PurityType::kPure));
  environment.value()->DefineType(L"bool", std::move(bool_type));

  auto int_type = MakeNonNullUnique<ObjectType>(VMType::Integer());
  int_type->AddField(
      L"tostring",
      NewCallback(pool, std::function<std::wstring(int)>([](int value) {
                    return std::to_wstring(value);
                  }),
                  VMType::PurityType::kPure));
  environment.value()->DefineType(L"int", std::move(int_type));

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
  environment.value()->DefineType(L"double", std::move(double_type));

  // TODO(easy, 2022-05-11): Pass by reference.
  VMTypeMapper<std::vector<int>*>::Export(pool, &environment.value().value());
  VMTypeMapper<std::set<int>*>::Export(pool, &environment.value().value());
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
  if (auto it = object_types_.find(symbol); it != object_types_.end()) {
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
    return &VMType::Integer();
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
  if (auto result = parent.value()->namespaces_.find(name);
      result != parent.value()->namespaces_.end()) {
    return result->second.ToRoot();
  }

  gc::Root<Environment> namespace_env =
      pool.NewRoot(MakeNonNullUnique<Environment>(parent.value()));
  auto [_, inserted] =
      parent.value()->namespaces_.insert({name, namespace_env.value()});
  CHECK(inserted);
  return namespace_env;
}

/* static */ std::optional<gc::Root<Environment>> Environment::LookupNamespace(
    gc::Root<Environment> source, const Namespace& name) {
  std::optional<gc::Ptr<Environment>> output = {source.value()};
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
  if (auto parent_environment = source.value()->parent_environment();
      parent_environment.has_value()) {
    return LookupNamespace(parent_environment->ToRoot(), name);
  }
  return std::nullopt;
}

void Environment::DefineType(const wstring& name,
                             NonNull<std::unique_ptr<ObjectType>> value) {
  object_types_.insert_or_assign(name, std::move(value));
}

std::unique_ptr<Value> Environment::Lookup(gc::Pool& pool,
                                           const Namespace& symbol_namespace,
                                           const wstring& symbol,
                                           VMType expected_type) {
  std::vector<NonNull<Value*>> values;
  PolyLookup(symbol_namespace, symbol, &values);
  for (auto& value : values) {
    if (auto callback = GetImplicitPromotion(value->type, expected_type);
        callback != nullptr) {
      return std::move(
          callback(pool, MakeNonNullUnique<Value>(*value)).get_unique());
    }
  }
  return nullptr;
}

void Environment::PolyLookup(const wstring& symbol,
                             std::vector<NonNull<Value*>>* output) {
  static const auto* empty_namespace = new Environment::Namespace();
  PolyLookup(*empty_namespace, symbol, output);
}

void Environment::PolyLookup(const Environment::Namespace& symbol_namespace,
                             const wstring& symbol,
                             std::vector<NonNull<Value*>>* output) {
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
    if (auto it = environment->table_.find(symbol);
        it != environment->table_.end()) {
      for (auto& entry : it->second) {
        output->push_back(entry.second.get());
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
    std::vector<NonNull<Value*>>* output) {
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
          output->push_back(entry.second.get());
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

void Environment::Define(const wstring& symbol,
                         NonNull<std::unique_ptr<Value>> value) {
  table_[symbol].insert_or_assign(value->type, std::move(value));
}

void Environment::Assign(const wstring& symbol,
                         NonNull<std::unique_ptr<Value>> value) {
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
  it->second.insert_or_assign(value->type, std::move(value));
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
  for (const std::pair<const std::wstring,
                       NonNull<std::unique_ptr<ObjectType>>>& entry :
       object_types_) {
    callback(entry.first, *entry.second);
  }
}

void Environment::ForEach(
    std::function<void(const wstring&, Value&)> callback) {
  if (parent_environment_.has_value()) {
    (*parent_environment_)->ForEach(callback);
  }
  ForEachNonRecursive(callback);
}

void Environment::ForEachNonRecursive(
    std::function<void(const std::wstring&, Value&)> callback) {
  for (auto& symbol_entry : table_) {
    for (const std::pair<const VMType,
                         language::NonNull<std::unique_ptr<Value>>>&
             type_entry : symbol_entry.second) {
      callback(symbol_entry.first, *type_entry.second);
    }
  }
}

}  // namespace afc::vm
namespace afc::language::gc {
std::vector<language::NonNull<std::shared_ptr<ControlFrame>>> Expand(
    const afc::vm::Environment& environment) {
  std::vector<language::NonNull<std::shared_ptr<ControlFrame>>> output;
  if (environment.parent_environment().has_value()) {
    output.push_back(environment.parent_environment()->control_frame());
  }
  return output;
}
}  // namespace afc::language::gc
