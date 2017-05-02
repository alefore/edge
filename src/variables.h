#ifndef __AFC_EDITOR_VARIABLES_H__
#define __AFC_EDITOR_VARIABLES_H__

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "predictor.h"
#include "vm/public/types.h"

namespace afc {
namespace editor {

using std::map;
using std::string;
using std::unique_ptr;

template <typename T>
class EdgeStruct;

template <typename T>
struct EdgeVariable {
 public:
  const wstring& name() const { return name_; }
  const wstring& description() const { return description_; }
  const T& default_value() const { return default_value_; }
  const size_t& position() const { return position_; }
  const Predictor& predictor() const { return predictor_; }

 private:
  // Instantiate it through EdgeStruct::AddVariable.
  EdgeVariable(const wstring& name,
               const wstring& description,
               const T& default_value,
               size_t position,
               const Predictor& predictor)
      : name_(name),
        description_(description),
        default_value_(default_value),
        position_(position),
        predictor_(predictor) {}

  wstring name_;
  wstring description_;
  T default_value_;
  size_t position_;
  // Used to predict values.
  Predictor predictor_;

  friend class EdgeStruct<T>;
};

template <typename T>
struct EdgeVariable<unique_ptr<T>> {
 public:
  const wstring& name() const { return name_; }
  const wstring& description() const { return description_; }
  const afc::vm::VMType& type() const { return type_; }
  const T& default_value() const { return nullptr; }
  const size_t& position() const { return position_; }
  const Predictor& predictor() const { return predictor_; }

 private:
  // Instantiate it through EdgeStruct::AddVariable.
  EdgeVariable(const wstring& name,
               const wstring& description,
               const afc::vm::VMType& type,
               size_t position,
               const Predictor& predictor)
      : name_(name),
        description_(description),
        type_(type),
        position_(position),
        predictor_(predictor) {}

  wstring name_;
  wstring description_;
  afc::vm::VMType type_;
  size_t position_;
  // Used to predict values.
  Predictor predictor_;

  friend class EdgeStruct<unique_ptr<T>>;
};

template <typename T>
class EdgeStructInstance {
 public:
  void CopyFrom(const EdgeStructInstance<T>& src);
  const T& Get(const EdgeVariable<T>* variable) const;
  void Set(const EdgeVariable<T>* variable, T value);

 private:
  // Instantiate it through EdgeStruct::NewInstance.
  EdgeStructInstance() {}

  // We use deque to workaround the fact that std::vector<bool> (doesn't return
  // references).
  std::deque<T> values_;

  friend class EdgeStruct<T>;
};

template <typename T>
class EdgeStructInstance<unique_ptr<T>> {
 public:
  void CopyFrom(const EdgeStructInstance<unique_ptr<T>>& src);
  const T* Get(const EdgeVariable<unique_ptr<T>>* variable) const;
  void Set(const EdgeVariable<unique_ptr<T>>* variable, unique_ptr<T> value);

 private:
  // Instantiate it through EdgeStruct::NewInstance.
  EdgeStructInstance() {}

  std::deque<unique_ptr<T>> values_;

  friend class EdgeStruct<unique_ptr<T>>;
};

using std::make_pair;

template <typename T>
class EdgeStruct {
 public:
  EdgeVariable<T>* AddVariable(
      const wstring& name, const wstring& description, const T& default_value);

  EdgeVariable<T>* AddVariable(
      const wstring& name, const wstring& description, const T& default_value,
      const Predictor& predictor);

  EdgeStructInstance<T> NewInstance() {
    EdgeStructInstance<T> instance;
    instance.values_.resize(variables_.size());
    for (const auto& v : variables_) {
      VLOG(5) << "Initializing variable: " << v.first << " = " << v.second->default_value();
      instance.values_[v.second->position()] = v.second->default_value();
    }
    return instance;
  }

  const EdgeVariable<T>* find_variable(const wstring& name) {
    auto it = variables_.find(name);
    return it == variables_.end() ? nullptr : it->second.get();
  }

  void RegisterVariableNames(vector<wstring>* output) {
    for (const auto& it : variables_) {
      output->push_back(it.first);
    }
  }

  const map<wstring, unique_ptr<EdgeVariable<T>>>& variables() const {
    return variables_;
  }

 private:
  map<wstring, unique_ptr<EdgeVariable<T>>> variables_;
};

template <typename T>
class EdgeStruct<unique_ptr<T>> {
 public:
  EdgeVariable<unique_ptr<T>>* AddVariable(
      const wstring& name, const wstring& description,
      const afc::vm::VMType& type);

  EdgeVariable<unique_ptr<T>>* AddVariable(
      const wstring& name, const wstring& description,
      const afc::vm::VMType& type, const Predictor& predictor);

  EdgeStructInstance<unique_ptr<T>> NewInstance() {
    EdgeStructInstance<unique_ptr<T>> instance;
    instance.values_.resize(variables_.size());
    for (const auto& v : variables_) {
      VLOG(5) << "Initializing unique_ptr variable: " << v.first;
      instance.values_[v.second->position()].reset(nullptr);
    }
    return instance;
  }

  const EdgeVariable<unique_ptr<T>>* find_variable(const wstring& name) {
    auto it = variables_.find(name);
    return it == variables_.end() ? nullptr : it->second.get();
  }

  void RegisterVariableNames(vector<wstring>* output) {
    for (const auto& it : variables_) {
      output->push_back(it.first);
    }
  }

 private:
  map<wstring, unique_ptr<EdgeVariable<unique_ptr<T>>>> variables_;
};

template <typename T>
void EdgeStructInstance<T>::CopyFrom(const EdgeStructInstance<T>& src) {
  values_ = src.values_;
}

template <typename T>
const T& EdgeStructInstance<T>::Get(const EdgeVariable<T>* variable) const {
  return values_.at(variable->position());
}

template <typename T>
const T* EdgeStructInstance<unique_ptr<T>>::Get(
    const EdgeVariable<unique_ptr<T>>* variable) const {
  return values_.at(variable->position()).get();
}

template <typename T>
void EdgeStructInstance<T>::Set(const EdgeVariable<T>* variable, T value) {
  assert(variable->position() <= values_.size());
  values_[variable->position()] = std::move(value);
}

template <typename T>
void EdgeStructInstance<unique_ptr<T>>::Set(
    const EdgeVariable<unique_ptr<T>>* variable, unique_ptr<T> value) {
  assert(variable->position() <= values_.size());
  values_[variable->position()] = std::move(value);
}

template <typename T>
EdgeVariable<T>* EdgeStruct<T>::AddVariable(
    const wstring& name, const wstring& description, const T& default_value) {
  return AddVariable(name, description, default_value, EmptyPredictor);
}

template <typename T>
EdgeVariable<T>* EdgeStruct<T>::AddVariable(
    const wstring& name, const wstring& description, const T& default_value,
    const Predictor& predictor) {
  auto it = variables_.emplace(make_pair(
      name,
      unique_ptr<EdgeVariable<T>>(new EdgeVariable<T>(
          name, description, default_value, variables_.size(), predictor))));
  return it.first->second.get();
}

template <typename T>
EdgeVariable<unique_ptr<T>>* EdgeStruct<unique_ptr<T>>::AddVariable(
    const wstring& name, const wstring& description,
    const afc::vm::VMType& type) {
  return AddVariable(name, description, type, EmptyPredictor);
}

template <typename T>
EdgeVariable<unique_ptr<T>>* EdgeStruct<unique_ptr<T>>::AddVariable(
    const wstring& name, const wstring& description,
    const afc::vm::VMType& type, const Predictor& predictor) {
  return variables_.emplace(
      make_pair(
          name,
          unique_ptr<EdgeVariable<unique_ptr<T>>>(
              new EdgeVariable<unique_ptr<T>>(name, description, type,
                  variables_.size(), predictor)))).first->second.get();
}

}  // namespace editor
}  // namespace afc

#endif
