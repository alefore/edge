#include "../public/environment.h"

#include <glog/logging.h>

#include "../public/callbacks.h"
#include "../public/types.h"
#include "../public/value.h"
#include "string.h"

namespace afc {
namespace vm {

namespace {

std::unique_ptr<Environment> BuildDefaultEnvironment() {
  auto environment = std::make_unique<Environment>();
  RegisterStringType(environment.get());

  auto bool_type = std::make_unique<ObjectType>(VMType::Bool());
  bool_type->AddField(L"tostring",
                      NewCallback(std::function<wstring(bool)>(
                          [](bool v) { return v ? L"true" : L"false"; })));
  environment->DefineType(L"bool", std::move(bool_type));

  auto int_type = std::make_unique<ObjectType>(VMType::Integer());
  int_type->AddField(L"tostring",
                     NewCallback(std::function<std::wstring(int)>(
                         [](int value) { return std::to_wstring(value); })));
  environment->DefineType(L"int", std::move(int_type));

  auto double_type = std::make_unique<ObjectType>(VMType::Double());
  double_type->AddField(
      L"tostring", NewCallback(std::function<std::wstring(double)>(
                       [](double value) { return std::to_wstring(value); })));
  double_type->AddField(
      L"round", NewCallback(std::function<int(double)>(
                    [](double value) { return static_cast<int>(value); })));
  environment->DefineType(L"double", std::move(double_type));
  return environment;
}

}  // namespace

Environment::Environment() : parent_environment_(nullptr) {}

Environment::Environment(Environment* parent_environment)
    : parent_environment_(parent_environment) {}

/* static */ Environment* Environment::GetDefault() {
  static auto environment = BuildDefaultEnvironment();
  return environment.get();
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

void Environment::DefineType(const wstring& name,
                             unique_ptr<ObjectType> value) {
  auto it = object_types_.insert(make_pair(name, nullptr));
  it.first->second = std::move(value);
}

Value* Environment::Lookup(const wstring& symbol) {
  auto it = table_.find(symbol);
  if (it != table_.end()) {
    return it->second.get();
  }
  if (parent_environment_ != nullptr) {
    return parent_environment_->Lookup(symbol);
  }

  return nullptr;
}

void Environment::Define(const wstring& symbol, unique_ptr<Value> value) {
  table_[symbol] = std::move(value);
}

void Environment::Assign(const wstring& symbol, unique_ptr<Value> value) {
  auto it = table_.find(symbol);
  if (it == table_.end()) {
    // TODO: Show the symbol.
    CHECK(parent_environment_ != nullptr)
        << "Environment::parent_environment_ is nullptr while trying to assign "
        << "a new value to a symbol `"
        << "..."
        << "`. This likely means that "
        << "the symbol is undefined (which the caller should have validated as "
        << "part of the compilation process).";
    parent_environment_->Assign(symbol, std::move(value));
    return;
  }
  it->second = std::move(value);
}

}  // namespace vm
}  // namespace afc
