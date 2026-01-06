//
// Neam Virtual Machine - Object system
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
struct Obj
{
  ObjType type;
  bool marked{false};
  Obj* next{nullptr};
};

enum class ObjType
{
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE
};

struct ObjString : Obj
{
  std::size_t length{0};
  char* chars{nullptr};
  uint32_t hash{0};
};

struct ObjFunction : Obj
{
  int arity{0};
  Chunk chunk;
  ObjString* name{nullptr};
};

using NativeFn = Value (*)(int argCount, Value* args);

struct ObjNative : Obj
{
  int arity{0};
  ObjString* name{nullptr};
  NativeFn function{nullptr};
};

ObjString* allocate_string(char* chars, std::size_t length, uint32_t hash);
ObjString* copy_string(const char* chars, std::size_t length);
ObjString* take_string(char* chars, std::size_t length);
ObjFunction* new_function();
ObjNative* new_native(ObjString* name, int arity, NativeFn function);

uint32_t hash_string(const char* key, std::size_t length);
}  // namespace neamc::vm
