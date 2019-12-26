#include "lambda.h"

#include <glog/logging.h>

#include "../internal/compilation.h"
#include "../public/environment.h"
#include "../public/value.h"

namespace afc::vm {
std::unique_ptr<Value> NewFunctionValue(
    std::wstring name, VMType expected_return_type,
    std::vector<VMType> argument_types,
    std::vector<std::wstring> argument_names, std::unique_ptr<Expression> body,
    std::shared_ptr<Environment> environment, std::wstring* error) {
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
  output->callback =
      [name, body = std::shared_ptr<Expression>(std::move(body)), environment,
       argument_names](vector<unique_ptr<Value>> args, Trampoline* trampoline) {
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
}  // namespace afc::vm
