#ifndef __AFC_EDITOR_VARIABLES_H__
#define __AFC_EDITOR_VARIABLES_H__

#include <glog/logging.h>

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/language/observers.h"
#include "src/language/wstring.h"
#include "src/predictor.h"
#include "vm/public/types.h"

namespace afc {
namespace editor {
template <typename T>
class EdgeStruct;

template <typename T>
struct EdgeVariable {
 public:
  const std::wstring& name() const { return name_; }
  const std::wstring& description() const { return description_; }
  const std::wstring& key() const { return key_; }
  const T& default_value() const { return default_value_; }
  const size_t& position() const { return position_; }
  const Predictor& predictor() const { return predictor_; }

 private:
  // Instantiate it through EdgeStruct::Add.
  EdgeVariable(const std::wstring& name, const std::wstring& description,
               const std::wstring key, const T& default_value, size_t position,
               const Predictor& predictor)
      : name_(name),
        description_(description),
        key_(key),
        default_value_(default_value),
        position_(position),
        predictor_(predictor) {}

  std::wstring name_;
  std::wstring description_;
  std::wstring key_;
  T default_value_;
  size_t position_;
  // Used to predict values.
  Predictor predictor_;

  friend class EdgeStruct<T>;
};

template <typename T>
struct EdgeVariable<std::unique_ptr<T>> {
 public:
  const std::wstring& name() const { return name_; }
  const std::wstring& description() const { return description_; }
  const afc::vm::Type& type() const { return type_; }
  const T& default_value() const { return nullptr; }
  const size_t& position() const { return position_; }
  const Predictor& predictor() const { return predictor_; }

 private:
  // Instantiate it through EdgeStruct::AddVariable.
  EdgeVariable(const std::wstring& name, const std::wstring& description,
               const afc::vm::Type& type, size_t position,
               const Predictor& predictor)
      : name_(name),
        description_(description),
        type_(type),
        position_(position),
        predictor_(predictor) {}

  std::wstring name_;
  std::wstring description_;
  afc::vm::Type type_;
  size_t position_;
  // Used to predict values.
  Predictor predictor_;

  friend class EdgeStruct<std::unique_ptr<T>>;
};

template <typename T>
class EdgeStructInstance {
 public:
  void CopyFrom(const EdgeStructInstance<T>& src);
  const T& Get(const EdgeVariable<T>* variable) const;
  void Set(const EdgeVariable<T>* variable, T value);
  language::Observable& ObserveValue(const EdgeVariable<T>* variable);

 private:
  // Instantiate it through EdgeStruct::NewInstance.
  EdgeStructInstance() {}

  // We use deque to workaround the fact that std::vector<bool> doesn't return
  // references.
  std::deque<language::ObservableValue<T>> values_;

  friend class EdgeStruct<T>;
};

template <typename T>
class EdgeStructInstance<std::unique_ptr<T>> {
 public:
  void CopyFrom(const EdgeStructInstance<std::unique_ptr<T>>& src);
  const T* Get(const EdgeVariable<std::unique_ptr<T>>* variable) const;
  void Set(const EdgeVariable<std::unique_ptr<T>>* variable,
           std::unique_ptr<T> value);
  language::Observable& ObserveValue(
      const EdgeVariable<std::unique_ptr<T>>* variable);

 private:
  // Instantiate it through EdgeStruct::NewInstance.
  EdgeStructInstance() {}

  std::deque<language::ObservableValue<std::unique_ptr<T>>> values_;

  friend class EdgeStruct<std::unique_ptr<T>>;
};

using std::make_pair;

template <typename T>
class VariableBuilder {
 public:
  EdgeVariable<T>* Build() {
    return parent_->AddVariable(name_, description_, key_, default_value_,
                                predictor_);
  };

  VariableBuilder& Name(std::wstring name) {
    name_ = name;
    return *this;
  }

  VariableBuilder& Description(std::wstring description) {
    description_ = description;
    return *this;
  }

  VariableBuilder& Key(std::wstring key) {
    key_ = key;
    return *this;
  }

  VariableBuilder& DefaultValue(T default_value) {
    default_value_ = default_value;
    return *this;
  }

  VariableBuilder& Predictor(Predictor predictor) {
    predictor_ = std::move(predictor);
    return *this;
  }

 private:
  friend class EdgeStruct<T>;
  VariableBuilder(EdgeStruct<T>* parent) : parent_(parent) {}

  EdgeStruct<T>* const parent_;
  std::wstring name_;
  std::wstring description_;
  std::wstring key_;
  afc::editor::Predictor predictor_ = EmptyPredictor;
  T default_value_ = T();
};

template <typename T>
class EdgeStruct {
 public:
  VariableBuilder<T> Add() { return VariableBuilder(this); }

  EdgeStructInstance<T> NewInstance() {
    EdgeStructInstance<T> instance;
    instance.values_.resize(variables_.size());
    for (const auto& v : variables_) {
      VLOG(5) << "Initializing variable: " << v.first << " = "
              << v.second->default_value();
      instance.values_[v.second->position()].Set(v.second->default_value());
    }
    return instance;
  }

  const EdgeVariable<T>* find_variable(const std::wstring& name) {
    auto it = variables_.find(name);
    return it == variables_.end() ? nullptr : it->second.get();
  }

  void RegisterVariableNames(std::vector<std::wstring>* output) {
    for (const auto& it : variables_) {
      output->push_back(it.first);
    }
  }

  const std::map<std::wstring, std::unique_ptr<EdgeVariable<T>>>& variables()
      const {
    return variables_;
  }

 private:
  friend class VariableBuilder<T>;

  EdgeVariable<T>* AddVariable(const std::wstring& name,
                               const std::wstring& description,
                               const std::wstring& key, const T& default_value,
                               const Predictor& predictor);

  std::map<std::wstring, std::unique_ptr<EdgeVariable<T>>> variables_;
};

template <typename T>
class EdgeStruct<std::unique_ptr<T>> {
 public:
  EdgeVariable<std::unique_ptr<T>>* AddVariable(const std::wstring& name,
                                                const std::wstring& description,
                                                const afc::vm::Type& type);

  EdgeVariable<std::unique_ptr<T>>* AddVariable(const std::wstring& name,
                                                const std::wstring& description,
                                                const afc::vm::Type& type,
                                                const Predictor& predictor);

  EdgeStructInstance<std::unique_ptr<T>> NewInstance() {
    EdgeStructInstance<std::unique_ptr<T>> instance;
    instance.values_.resize(variables_.size());
    for (const auto& v : variables_) {
      VLOG(5) << "Initializing std::unique_ptr variable: " << v.first;
      instance.values_[v.second->position()].reset(nullptr);
    }
    return instance;
  }

  const EdgeVariable<std::unique_ptr<T>>* find_variable(
      const std::wstring& name) {
    auto it = variables_.find(name);
    return it == variables_.end() ? nullptr : it->second.get();
  }

  void RegisterVariableNames(std::vector<std::wstring>* output) {
    for (const auto& it : variables_) {
      output->push_back(it.first);
    }
  }

  const std::map<std::wstring, std::unique_ptr<EdgeVariable<T>>>& variables()
      const {
    return variables_;
  }

 private:
  std::map<std::wstring, std::unique_ptr<EdgeVariable<std::unique_ptr<T>>>>
      variables_;
};

template <typename T>
void EdgeStructInstance<T>::CopyFrom(const EdgeStructInstance<T>& src) {
  values_ = src.values_;
}

template <typename T>
const T& EdgeStructInstance<T>::Get(const EdgeVariable<T>* variable) const {
  CHECK_LE(variable->position(), values_.size());
  return values_.at(variable->position()).Get().value();
}

template <typename T>
const T* EdgeStructInstance<std::unique_ptr<T>>::Get(
    const EdgeVariable<std::unique_ptr<T>>* variable) const {
  CHECK_LE(variable->position(), values_.size());
  return values_.at(variable->position()).get().value().get();
}

template <typename T>
void EdgeStructInstance<T>::Set(const EdgeVariable<T>* variable, T value) {
  CHECK_LE(variable->position(), values_.size());
  values_[variable->position()].Set(std::move(value));
}

template <typename T>
void EdgeStructInstance<std::unique_ptr<T>>::Set(
    const EdgeVariable<std::unique_ptr<T>>* variable,
    std::unique_ptr<T> value) {
  CHECK_GE(variable->position(), values_.size());
  values_[variable->position()].Set(std::move(value));
}

template <typename T>
language::Observable& EdgeStructInstance<T>::ObserveValue(
    const EdgeVariable<T>* variable) {
  CHECK_LE(variable->position(), values_.size());
  return values_.at(variable->position());
}

template <typename T>
language::Observable& EdgeStructInstance<std::unique_ptr<T>>::ObserveValue(
    const EdgeVariable<std::unique_ptr<T>>* variable) {
  CHECK_LE(variable->position(), values_.size());
  return values_.at(variable->position());
}

template <typename T>
EdgeVariable<T>* EdgeStruct<T>::AddVariable(const std::wstring& name,
                                            const std::wstring& description,
                                            const std::wstring& key,
                                            const T& default_value,
                                            const Predictor& predictor) {
  auto it = variables_.emplace(
      make_pair(name, std::unique_ptr<EdgeVariable<T>>(new EdgeVariable<T>(
                          name, description, key, default_value,
                          variables_.size(), predictor))));
  CHECK(it.second);
  return it.first->second.get();
}

template <typename T>
EdgeVariable<std::unique_ptr<T>>* EdgeStruct<std::unique_ptr<T>>::AddVariable(
    const std::wstring& name, const std::wstring& description,
    const afc::vm::Type& type, const Predictor& predictor) {
  return variables_
      .emplace(make_pair(
          name,
          std::unique_ptr<EdgeVariable<std::unique_ptr<T>>>(
              new EdgeVariable<std::unique_ptr<T>>(
                  name, description, type, variables_.size(), predictor))))
      .first->second.get();
}

}  // namespace editor
}  // namespace afc

#endif
