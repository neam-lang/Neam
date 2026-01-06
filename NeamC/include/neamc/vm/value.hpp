//
// Neam Virtual Machine - Value model
//

#pragma once

#include <cstddef>
#include <cstdint>

namespace neamc::vm
{
enum class ObjType;
struct Obj;
struct ObjString;
struct ObjFunction;
struct ObjNative;

enum class ValueType
{
  Nil,
  Bool,
  Number,
  Obj
};

struct Value
{
  ValueType type{ValueType::Nil};
  union
  {
    bool boolean;
    double number;
    Obj* obj;
  } as{};

  static Value Nil();
  static Value Bool(bool value);
  static Value Number(double value);
  static Value ObjVal(Obj* object);

  static Value String(const char* chars, std::size_t length);
  static Value FunctionValue(ObjFunction* function);
  static Value Native(ObjNative* native);

  bool is_nil() const { return type == ValueType::Nil; }
  bool is_bool() const { return type == ValueType::Bool; }
  bool is_number() const { return type == ValueType::Number; }
  bool is_obj() const { return type == ValueType::Obj; }
  bool is_string() const;
  bool is_function() const;
  bool is_native() const;

  bool as_bool() const;
  double as_number() const;
  Obj* as_obj() const;
};

bool is_obj_type(const Value& value, ObjType type);
ObjString* as_string(const Value& value);
ObjFunction* as_function(const Value& value);
ObjNative* as_native(const Value& value);

}  // namespace neamc::vm
