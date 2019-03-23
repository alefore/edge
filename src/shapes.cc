#include "src/shapes.h"

#include <set>

#include <glog/logging.h>

#include "src/line_column.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"

namespace afc {
namespace editor {
// Coordinates are in range [0.0, 1.0).
struct Point {
  double x;
  double y;
};

struct LineColumnSet {
  std::set<LineColumn> positions;
};

struct Line {
  Point start;
  Point end;
};

struct LineVector {
  std::vector<Line> lines;
};
}  // namespace editor
namespace vm {
template <>
struct VMTypeMapper<editor::Point> {
  static editor::Point get(Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"ShapesPoint");
    CHECK(value->user_value != nullptr);
    return *static_cast<editor::Point*>(value->user_value.get());
  }

  static Value::Ptr New(editor::Point value) {
    return Value::NewObject(
        L"ShapesPoint", shared_ptr<void>(new editor::Point(value), [](void* v) {
          delete static_cast<editor::Point*>(v);
        }));
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::Point>::vmtype =
    VMType::ObjectType(L"ShapesPoint");

template <>
struct VMTypeMapper<editor::LineColumnSet*> {
  static editor::LineColumnSet* get(Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"ShapesLineColumnSet");
    CHECK(value->user_value != nullptr);
    return static_cast<editor::LineColumnSet*>(value->user_value.get());
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::LineColumnSet*>::vmtype =
    VMType::ObjectType(L"ShapesLineColumnSet");

template <>
struct VMTypeMapper<editor::Line> {
  static editor::Line get(Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"ShapesLine");
    CHECK(value->user_value != nullptr);
    return *static_cast<editor::Line*>(value->user_value.get());
  }

  static Value::Ptr New(editor::Line value) {
    return Value::NewObject(
        L"ShapesLine", shared_ptr<void>(new editor::Line(value), [](void* v) {
          delete static_cast<editor::Line*>(v);
        }));
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::Line>::vmtype =
    VMType::ObjectType(L"ShapesLine");

template <>
struct VMTypeMapper<editor::LineVector*> {
  static editor::LineVector* get(Value* value) {
    return static_cast<editor::LineVector*>(value->user_value.get());
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::LineVector*>::vmtype =
    VMType::ObjectType(L"ShapesLineVector");
}  // namespace vm
namespace editor {

using namespace afc::vm;

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

void DefineLine(Environment* environment) {
  auto line_type = std::make_unique<ObjectType>(L"ShapesLine");

  environment->Define(L"ShapesLine",
                      vm::NewCallback(std::function<Line(Point, Point)>(
                          [](Point start, Point end) {
                            Line output = {start, end};
                            return output;
                          })));

  line_type->AddField(L"start", vm::NewCallback(std::function<Point(Line)>(
                                    [](Line line) { return line.start; })));
  line_type->AddField(L"end", vm::NewCallback(std::function<Point(Line)>(
                                  [](Line line) { return line.end; })));

  environment->DefineType(L"ShapesLine", std::move(line_type));
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

void DefineLineVector(Environment* environment) {
  auto line_vector_type = std::make_unique<ObjectType>(L"ShapesLineVector");

  environment->Define(
      L"ShapesLineVector",
      Value::NewFunction({VMType::ObjectType(line_vector_type.get())},
                         [](std::vector<Value::Ptr> args) {
                           CHECK(args.empty());
                           return Value::NewObject(
                               L"ShapesLineVector",
                               std::make_shared<LineVector>());
                         }));

  line_vector_type->AddField(
      L"size", vm::NewCallback(std::function<int(LineVector*)>(
                   [](LineVector* s) { return s->lines.size(); })));
  line_vector_type->AddField(
      L"get", vm::NewCallback(std::function<Line(LineVector*, int)>(
                  [](LineVector* s, int i) { return s->lines.at(i); })));
  line_vector_type->AddField(
      L"erase", vm::NewCallback(std::function<void(LineVector*, int)>(
                    [](LineVector* s, int i) {
                      return s->lines.erase(s->lines.begin() + i);
                    })));
  line_vector_type->AddField(
      L"push_back", vm::NewCallback(std::function<void(LineVector*, Line)>(
                        [](LineVector* s, Line e) { s->lines.push_back(e); })));
  environment->DefineType(L"ShapesLineVector", std::move(line_vector_type));
}

void InitShapes(vm::Environment* environment) {
  DefinePoint(environment);
  DefineLineColumnSet(environment);
  DefineLine(environment);
  DefineLineVector(environment);
}

}  // namespace editor
}  // namespace afc
