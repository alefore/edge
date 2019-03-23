#include "src/shapes.h"

#include <set>

#include <glog/logging.h>

#include "src/line_column.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"

namespace afc {
namespace editor {
struct LineColumnSet;
}  // namespace editor
namespace vm {
template <>
struct VMTypeMapper<editor::LineColumnSet*> {
  static editor::LineColumnSet* get(Value* value) {
    return static_cast<editor::LineColumnSet*>(value->user_value.get());
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::LineColumnSet*>::vmtype =
    VMType::ObjectType(L"ShapesLineColumnSet");

}  // namespace vm
namespace editor {

using namespace afc::vm;

// Coordinates are in range [0.0, 1.0).
struct Point {
  double x;
  double y;
};

struct LineColumnSet {
  std::set<LineColumn> positions;
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

void DefineLineColumnSet(Environment* environment) {
  auto line_column_set_type =
      std::make_unique<ObjectType>(L"ShapesLineColumnSet");

  environment->Define(
      L"ShapesLineColumnSet",
      Value::NewFunction({VMType::ObjectType(line_column_set_type.get())},
                         [](std::vector<Value::Ptr> args) {
                           CHECK(args.empty());
                           return Value::NewObject(
                               L"ShapesLineColumnSet",
                               std::make_shared<LineColumnSet>());
                         }));

  line_column_set_type->AddField(
      L"size", vm::NewCallback(std::function<int(LineColumnSet*)>(
                   [](LineColumnSet* s) { return s->positions.size(); })));
  line_column_set_type->AddField(
      L"contains",
      vm::NewCallback(std::function<bool(LineColumnSet*, LineColumn)>(
          [](LineColumnSet* s, LineColumn p) {
            return s->positions.count(p) > 0;
          })));
  line_column_set_type->AddField(
      L"get", vm::NewCallback(std::function<LineColumn(LineColumnSet*, int)>(
                  [](LineColumnSet* s, int i) {
                    auto it = s->positions.begin();
                    std::advance(it, i);
                    return *it;
                  })));
  line_column_set_type->AddField(
      L"insert",
      vm::NewCallback(std::function<void(LineColumnSet*, LineColumn)>(
          [](LineColumnSet* s, LineColumn e) { s->positions.insert(e); })));
  environment->DefineType(L"ShapesLineColumnSet",
                          std::move(line_column_set_type));
}

void InitShapes(vm::Environment* environment) {
  DefinePoint(environment);
  DefineLineColumnSet(environment);
}

}  // namespace editor
}  // namespace afc
