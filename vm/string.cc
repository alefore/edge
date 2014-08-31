#include "string.h"

#include <cassert>

#include "vm.h"

namespace afc {
namespace vm {

void RegisterStringType(Environment* environment) {
  unique_ptr<ObjectType> string_type(new ObjectType("string"));
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 1);
          assert(args[0]->type == VMType::VM_STRING);
          return Value::NewInteger(args[0]->str.size());
        };
    string_type->AddField("size", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 3);
          assert(args[0]->type == VMType::VM_STRING);
          if (args[1]->integer < 0
              || args[2]->integer < 0
              || (static_cast<size_t>(args[1]->integer + args[2]->integer)
                  > args[0]->str.size())) {
            return Value::NewString("");
          }
          return Value::NewString(
              args[0]->str.substr(args[1]->integer, args[2]->integer));
        };
    string_type->AddField("substr", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          return Value::NewBool(args[0]->str.empty());
        };
    string_type->AddField("empty", std::move(callback));
  }

  environment->DefineType("string", std::move(string_type));
}

}  // namespace vm
}  // namespace afc
