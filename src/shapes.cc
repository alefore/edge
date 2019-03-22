#include "src/shapes.h"

#include <glog/logging.h>

#include "src/vm/public/types.h"
#include "src/vm/public/value.h"

namespace afc {
namespace editor {

using namespace afc::vm;

struct Point {
  double x;
  double y;
};

void DefinePoint(Environment* environment) {
  auto point_type = std::make_unique<ObjectType>(L"ShapesPoint");

  environment->Define(
      L"ShapesPoint",
      Value::NewFunction({VMType::ObjectType(point_type.get()),
                          VMType::Double(), VMType::Double()},
                         [](std::vector<Value::Ptr> args) {
                           CHECK_EQ(args.size(), size_t(2));
                           CHECK_EQ(args[0]->type, VMType::VM_DOUBLE);
                           CHECK_EQ(args[1]->type, VMType::VM_DOUBLE);
                           auto point = std::make_shared<Point>();
                           point->x = args[0]->double_value;
                           point->y = args[1]->double_value;
                           return Value::NewObject(L"ShapesPoint", point);
                         }));

  point_type->AddField(
      L"x", Value::NewFunction(
                {VMType::Double(), VMType::ObjectType(point_type.get())},
                [](std::vector<Value::Ptr> args) {
                  CHECK_EQ(args.size(), size_t(1));
                  CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
                  auto point = static_cast<Point*>(args[0]->user_value.get());
                  CHECK(point != nullptr);
                  return Value::NewDouble(point->x);
                }));

  point_type->AddField(
      L"y", Value::NewFunction(
                {VMType::Double(), VMType::ObjectType(point_type.get())},
                [](std::vector<Value::Ptr> args) {
                  CHECK_EQ(args.size(), size_t(1));
                  CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
                  auto point = static_cast<Point*>(args[0]->user_value.get());
                  CHECK(point != nullptr);
                  return Value::NewDouble(point->y);
                }));

  environment->DefineType(L"ShapesPoint", std::move(point_type));
}

void InitShapes(vm::Environment* environment) { DefinePoint(environment); }

}  // namespace editor
}  // namespace afc
