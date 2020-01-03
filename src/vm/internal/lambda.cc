#include "lambda.h"

#include <glog/logging.h>

#include "../internal/compilation.h"
#include "../public/constant_expression.h"
#include "../public/environment.h"
#include "../public/value.h"

namespace afc::vm {
namespace {}  // namespace

std::unique_ptr<UserFunction> UserFunction::New(
    Compilation* compilation, std::wstring return_type,
    std::optional<std::wstring> name,
    std::vector<std::pair<VMType, wstring>>* args) {
  if (args == nullptr) {
    return nullptr;
  }
  const VMType* return_type_def =
      compilation->environment->LookupType(return_type);
  if (return_type_def == nullptr) {
    compilation->errors.push_back(L"Unknown return type: \"" + return_type +
                                  L"\"");
    return nullptr;
  }

  auto output = std::make_unique<UserFunction>();
  output->type.type = VMType::FUNCTION;
  output->type.type_arguments.push_back(*return_type_def);
  for (pair<VMType, wstring> arg : *args) {
    output->type.type_arguments.push_back(arg.first);
    output->argument_names.push_back(arg.second);
  }
  if (name.has_value()) {
    output->name = name.value();
    compilation->environment->Define(name.value(),
                                     std::make_unique<Value>(output->type));
  }
  compilation->environment = new Environment(compilation->environment);
  for (pair<VMType, wstring> arg : *args) {
    compilation->environment->Define(arg.second,
                                     std::make_unique<Value>(arg.first));
  }
  return output;
}

std::unique_ptr<Value> UserFunction::BuildValue(
    Compilation* compilation, std::unique_ptr<Expression> body,
    std::wstring* error) {
  auto environment = std::make_shared<Environment>(compilation->environment);
  compilation->environment = compilation->environment->parent_environment();

  std::vector<VMType> argument_types(type.type_arguments.cbegin() + 1,
                                     type.type_arguments.cend());

  VMType expected_return_type = *type.type_arguments.cbegin();
  auto deduced_types = body->ReturnTypes();
  if (deduced_types.empty()) {
    deduced_types.insert(VMType::Void());
  }
  if (deduced_types.size() > 1) {
    *error = L"Found multiple return types: ";
    std::wstring separator;
    for (const auto& type : deduced_types) {
      *error += separator + L"`" + type.ToString() + L"`";
      separator = L", ";
    }
    return nullptr;
  } else if (deduced_types.find(expected_return_type) == deduced_types.end()) {
    *error = L"Expected a return type of `" + expected_return_type.ToString() +
             L"` but found `" + deduced_types.cbegin()->ToString() + L"`.";
    return nullptr;
  }

  auto output = std::make_unique<Value>(VMType::FUNCTION);
  output->type.type_arguments.push_back(expected_return_type);
  for (auto& argument_type : argument_types) {
    output->type.type_arguments.push_back(argument_type);
  }
  output->callback = [body = std::shared_ptr<Expression>(std::move(body)),
                      environment, argument_names = argument_names](
                         vector<unique_ptr<Value>> args,
                         Trampoline* trampoline) {
    CHECK_EQ(args.size(), argument_names.size())
        << "Invalid number of arguments for function.";
    for (size_t i = 0; i < args.size(); i++) {
      environment->Define(argument_names[i], std::move(args[i]));
    }
    trampoline->SetReturnContinuation(
        [original_trampoline = *trampoline](std::unique_ptr<Value> value,
                                            Trampoline* trampoline) {
          CHECK(value != nullptr);
          // We have to make a copy because assigning to *trampoline may
          // delete us (and thus deletes original_trampoline as it is being
          // read).
          Trampoline tmp_copy = original_trampoline;
          *trampoline = tmp_copy;
          trampoline->Return(std::move(value));
        });
    trampoline->SetEnvironment(environment.get());
    trampoline->Bounce(body.get(), body->Types()[0],
                       [body](Value::Ptr value, Trampoline* trampoline) {
                         trampoline->Return(std::move(value));
                       });
  };
  return output;
}

std::unique_ptr<Expression> UserFunction::BuildExpression(
    Compilation* compilation, std::unique_ptr<Expression> body,
    std::wstring* error) {
  auto value = BuildValue(compilation, std::move(body), error);
  if (value == nullptr) return nullptr;
  return NewConstantExpression(std::move(value));
}

}  // namespace afc::vm
