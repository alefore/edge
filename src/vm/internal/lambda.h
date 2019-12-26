#ifndef __AFC_VM_LAMBDA_H__
#define __AFC_VM_LAMBDA_H__

#include <memory>

#include "../public/vm.h"

namespace afc::vm {
std::unique_ptr<Value> NewFunctionValue(
    std::wstring name, VMType type, std::vector<std::wstring> argument_names,
    std::unique_ptr<Expression> body, std::shared_ptr<Environment> environment);
}

#endif  // __AFC_VM_LAMBDA_H__
