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

template <>
const VMType VMTypeMapper<std::vector<int>*>::vmtype =
    VMType::ObjectType(L"VectorInt");

template <>
const VMType VMTypeMapper<std::set<int>*>::vmtype =
    VMType::ObjectType(L"SetInt");

std::shared_ptr<Environment> BuildDefaultEnvironment() {
  auto environment = std::make_shared<Environment>();
  RegisterStringType(environment.get());
  RegisterNumberFunctions(environment.get());
  RegisterTimeType(environment.get());
  auto bool_type = std::make_unique<ObjectType>(VMType::Bool());
  bool_type->AddField(L"tostring",
                      NewCallback(std::function<wstring(bool)>([](bool v) {
                                    return v ? L"true" : L"false";
                                  }),
                                  VMType::PurityType::kPure));
  environment->DefineType(L"bool", std::move(bool_type));

  auto int_type = std::make_unique<ObjectType>(VMType::Integer());
  int_type->AddField(
      L"tostring", NewCallback(std::function<std::wstring(int)>([](int value) {
                                 return std::to_wstring(value);
                               }),
                               VMType::PurityType::kPure));
  environment->DefineType(L"int", std::move(int_type));

  auto double_type = std::make_unique<ObjectType>(VMType::Double());
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
  environment->DefineType(L"double", std::move(double_type));

  VMTypeMapper<std::vector<int>*>::Export(environment.get());
  VMTypeMapper<std::set<int>*>::Export(environment.get());
  return environment;
}

}  // namespace

void Environment::Clear() {
  object_types_.clear();
  table_.clear();
}

/* static */ const std::shared_ptr<Environment>& Environment::GetDefault() {
  static std::shared_ptr<Environment> environment = BuildDefaultEnvironment();
  return environment;
}

const ObjectType* Environment::LookupObjectType(const wstring& symbol) {
  auto it = object_types_.find(symbol);
  if (it != object_types_.end()) {
    return it->second.get();
  }
  if (parent_environment_ != nullptr) {
    return parent_environment_->LookupObjectType(symbol);
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

Environment::Environment(std::shared_ptr<Environment> parent_environment)
    : parent_environment_(std::move(parent_environment)) {}

/* static */ std::shared_ptr<Environment> Environment::NewNamespace(
    std::shared_ptr<Environment> parent, std::wstring name) {
  if (auto previous = LookupNamespace(parent, {name}); previous != nullptr) {
    return previous;
  }
  auto result = parent->namespaces_.insert({name, nullptr}).first;
  if (result->second == nullptr) {
    result->second = std::make_shared<Environment>(std::move(parent));
  }
  return result->second;
}

/* static */ std::shared_ptr<Environment> Environment::LookupNamespace(
    std::shared_ptr<Environment> source, const Namespace& name) {
  if (source == nullptr) return nullptr;
  auto output = source;
  for (auto& n : name) {
    if (auto it = output->namespaces_.find(n);
        it != output->namespaces_.end()) {
      output = it->second;
      continue;
    }
    output = nullptr;
    break;
  }
  if (output != nullptr) {
    return output;
  }
  return LookupNamespace(source->parent_environment(), name);
}

void Environment::DefineType(const wstring& name,
                             unique_ptr<ObjectType> value) {
  auto it = object_types_.insert(make_pair(name, nullptr));
  it.first->second = std::move(value);
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

void Environment::PolyLookup(const Environment::Namespace& symbol_namespace,
                             const wstring& symbol,
                             std::vector<Value*>* output) {
  auto environment = this;
  for (auto& n : symbol_namespace) {
    auto it = environment->namespaces_.find(n);
    if (it == environment->namespaces_.end()) {
      environment = nullptr;
      break;
    }
    environment = it->second.get();
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
  if (parent_environment_ != nullptr) {
    parent_environment_->PolyLookup(symbol_namespace, symbol, output);
  }
}

void Environment::CaseInsensitiveLookup(
    const Environment::Namespace& symbol_namespace, const wstring& symbol,
    std::vector<Value*>* output) {
  auto environment = this;
  for (auto& n : symbol_namespace) {
    auto it = environment->namespaces_.find(n);
    if (it == environment->namespaces_.end()) {
      environment = nullptr;
      break;
    }
    environment = it->second.get();
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
  if (parent_environment_ != nullptr) {
    parent_environment_->CaseInsensitiveLookup(symbol_namespace, symbol,
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
    CHECK(parent_environment_ != nullptr)
        << "Environment::parent_environment_ is nullptr while trying to "
           "assign a new value to a symbol `...`. This likely means that the "
           "symbol is undefined (which the caller should have validated as "
           "part of the compilation process).";
    parent_environment_->Assign(symbol, std::move(value));
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
    std::function<void(const wstring&, ObjectType*)> callback) {
  if (parent_environment_ != nullptr) {
    parent_environment_->ForEachType(callback);
  }
  for (auto& entry : object_types_) {
    callback(entry.first, entry.second.get());
  }
}

void Environment::ForEach(
    std::function<void(const wstring&, Value*)> callback) {
  if (parent_environment_ != nullptr) {
    parent_environment_->ForEach(callback);
  }
  ForEachNonRecursive(callback);
}

void Environment::ForEachNonRecursive(
    std::function<void(const wstring&, Value*)> callback) {
  for (auto& symbol_entry : table_) {
    for (auto& type_entry : symbol_entry.second) {
      callback(symbol_entry.first, type_entry.second.get());
    }
  }
}

}  // namespace afc::vm
