#include "src/vm/public/environment.h"

#include <glog/logging.h>

#include <map>
#include <set>

#include "numbers.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/set.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"
#include "string.h"
#include "time.h"
#include "types_promotion.h"

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
      pool.NewRoot(std::make_unique<Environment>());
  RegisterStringType(environment.value().value());
  RegisterNumberFunctions(environment.value().value());
  RegisterTimeType(environment.value().value());
  auto bool_type = MakeNonNullUnique<ObjectType>(VMType::Bool());
  bool_type->AddField(L"tostring",
                      NewCallback(std::function<wstring(bool)>([](bool v) {
                                    return v ? L"true" : L"false";
                                  }),
                                  VMType::PurityType::kPure));
  environment.value()->DefineType(L"bool", std::move(bool_type));

  auto int_type = MakeNonNullUnique<ObjectType>(VMType::Integer());
  int_type->AddField(
      L"tostring", NewCallback(std::function<std::wstring(int)>([](int value) {
                                 return std::to_wstring(value);
                               }),
                               VMType::PurityType::kPure));
  environment.value()->DefineType(L"int", std::move(int_type));

  auto double_type = MakeNonNullUnique<ObjectType>(VMType::Double());
  double_type->AddField(
      L"tostring",
      NewCallback(std::function<std::wstring(double)>(
                      [](double value) { return std::to_wstring(value); }),
                  VMType::PurityType::kPure));
  double_type->AddField(
      L"round", NewCallback(std::function<int(double)>([](double value) {
                              return static_cast<int>(value);
                            }),
                            VMType::PurityType::kPure));
  environment.value()->DefineType(L"double", std::move(double_type));

  VMTypeMapper<std::vector<int>*>::Export(environment.value().value());
  VMTypeMapper<std::set<int>*>::Export(environment.value().value());
  return environment;
}

void Environment::Clear() {
  object_types_.clear();
  table_.clear();
}

const ObjectType* Environment::LookupObjectType(const wstring& symbol) {
  if (auto it = object_types_.find(symbol); it != object_types_.end()) {
    return it->second.get().get();
  }
  if (parent_environment_.value().value() != nullptr) {
    return parent_environment_.value()->LookupObjectType(symbol);
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

Environment::Environment(gc::Root<Environment> parent_environment)
    : parent_environment_(std::move(parent_environment)) {}

/* static */ gc::Root<Environment> Environment::NewNamespace(
    gc::Pool& pool, gc::Root<Environment> parent, std::wstring name) {
  if (auto previous = LookupNamespace(parent, {name});
      previous.value().value() != nullptr) {
    return previous;
  }
  if (auto result = parent.value()->namespaces_.find(name);
      result != parent.value()->namespaces_.end()) {
    return result->second;
  }

  gc::Root<Environment> namespace_env =
      pool.NewRoot(std::make_unique<Environment>(parent));
  auto [_, inserted] =
      parent.value()->namespaces_.insert({name, namespace_env});
  CHECK(inserted);
  return namespace_env;
}

/* static */ gc::Root<Environment> Environment::LookupNamespace(
    gc::Root<Environment> source, const Namespace& name) {
  if (source.value().value() == nullptr) return gc::Ptr<Environment>().ToRoot();
  gc::Root<Environment> output = source;
  for (auto& n : name) {
    if (auto it = output.value()->namespaces_.find(n);
        it != output.value()->namespaces_.end()) {
      output = it->second;
      continue;
    }
    output = gc::Ptr<Environment>().ToRoot();
    break;
  }
  if (output.value().value() != nullptr) {
    return output;
  }
  return LookupNamespace(source.value()->parent_environment(), name);
}

void Environment::DefineType(const wstring& name,
                             NonNull<std::unique_ptr<ObjectType>> value) {
  object_types_.insert_or_assign(name, std::move(value));
}

std::unique_ptr<Value> Environment::Lookup(const Namespace& symbol_namespace,
                                           const wstring& symbol,
                                           VMType expected_type) {
  std::vector<Value*> values;
  PolyLookup(symbol_namespace, symbol, &values);
  for (auto& value : values) {
    if (auto callback = GetImplicitPromotion(value->type, expected_type);
        callback != nullptr) {
      return std::move(callback(MakeNonNullUnique<Value>(*value)).get_unique());
    }
  }
  return nullptr;
}

void Environment::PolyLookup(const wstring& symbol,
                             std::vector<Value*>* output) {
  static const auto* empty_namespace = new Environment::Namespace();
  PolyLookup(*empty_namespace, symbol, output);
}

// TODO(easy, 2022-05-02): Make the vector contain NonNull pointers.
void Environment::PolyLookup(const Environment::Namespace& symbol_namespace,
                             const wstring& symbol,
                             std::vector<Value*>* output) {
  Environment* environment = this;
  for (auto& n : symbol_namespace) {
    auto it = environment->namespaces_.find(n);
    if (it == environment->namespaces_.end()) {
      environment = nullptr;
      break;
    }
    environment = it->second.value().value();
  }
  if (environment != nullptr) {
    if (auto it = environment->table_.find(symbol);
        it != environment->table_.end()) {
      for (auto& entry : it->second) {
        // TODO(easy, 2022-05-02): Get rid of 2nd get.
        output->push_back(entry.second.get().get());
      }
    }
  }
  // Deliverately ignoring `environment`:
  if (parent_environment_.value().value() != nullptr) {
    parent_environment_.value()->PolyLookup(symbol_namespace, symbol, output);
  }
}

void Environment::CaseInsensitiveLookup(
    const Environment::Namespace& symbol_namespace, const wstring& symbol,
    std::vector<Value*>* output) {
  Environment* environment = this;
  for (auto& n : symbol_namespace) {
    auto it = environment->namespaces_.find(n);
    if (it == environment->namespaces_.end()) {
      environment = nullptr;
      break;
    }
    environment = it->second.value().value();
  }
  if (environment != nullptr) {
    for (auto& item : environment->table_) {
      if (wcscasecmp(item.first.c_str(), symbol.c_str()) == 0) {
        for (auto& entry : item.second) {
          // TODO(easy, 2022-05-02): Get rid of 2nd get. Change output type.
          output->push_back(entry.second.get().get());
        }
      }
    }
  }
  // Deliverately ignoring `environment`:
  if (parent_environment_.value().value() != nullptr) {
    parent_environment_.value()->CaseInsensitiveLookup(symbol_namespace, symbol,
                                                       output);
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
    CHECK(parent_environment_.value().value() != nullptr)
        << "Environment::parent_environment_ is nullptr while trying to "
           "assign a new value to a symbol `...`. This likely means that the "
           "symbol is undefined (which the caller should have validated as "
           "part of the compilation process).";
    parent_environment_.value()->Assign(symbol, std::move(value));
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
  if (parent_environment_.value().value() != nullptr) {
    parent_environment_.value()->ForEachType(callback);
  }
  for (const std::pair<const std::wstring,
                       NonNull<std::unique_ptr<ObjectType>>>& entry :
       object_types_) {
    callback(entry.first, *entry.second);
  }
}

void Environment::ForEach(
    std::function<void(const wstring&, Value&)> callback) {
  if (parent_environment_.value().value() != nullptr) {
    parent_environment_.value()->ForEach(callback);
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
    const afc::vm::Environment&) {
  return {};
}
}  // namespace afc::language::gc
