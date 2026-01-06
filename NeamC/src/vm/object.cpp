//
// Neam Virtual Machine - Object system implementation
//

#include "neamc/vm/object.hpp"

#include <cstring>

#include "neamc/vm/memory.hpp"
#include "neamc/vm/table.hpp"
#include "neamc/vm/vm.hpp"

namespace neamc::vm
{
ObjString* allocate_string(char* chars, std::size_t length, uint32_t hash)
{
  auto* string = allocate_object<ObjString>(ObjType::OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  return string;
}

ObjString* take_string(char* chars, std::size_t length)
{
  const auto hash = hash_string(chars, length);
  return allocate_string(chars, length, hash);
}

ObjString* copy_string(const char* chars, std::size_t length)
{
  const auto hash = hash_string(chars, length);
  if (current_vm)
  {
    if (auto* interned = current_vm->strings().find_string(chars, length, hash))
    {
      return interned;
    }
  }
  char* heap_chars = ALLOCATE(char, length + 1);
  std::memcpy(heap_chars, chars, length);
  heap_chars[length] = '\0';
  auto* string = allocate_string(heap_chars, length, hash);
  if (current_vm)
  {
    current_vm->strings().set(string, Value::Nil());
  }
  return string;
}

ObjFunction* new_function()
{
  auto* function = allocate_object<ObjFunction>(ObjType::OBJ_FUNCTION);
  function->arity = 0;
  function->name = nullptr;
  return function;
}

ObjNative* new_native(ObjString* name, int arity, NativeFn function)
{
  auto* native = allocate_object<ObjNative>(ObjType::OBJ_NATIVE);
  native->arity = arity;
  native->name = name;
  native->function = function;
  return native;
}

uint32_t hash_string(const char* key, std::size_t length)
{
  constexpr uint32_t fnv_offset = 2166136261u;
  constexpr uint32_t fnv_prime = 16777619u;
  uint32_t hash = fnv_offset;
  for (std::size_t i = 0; i < length; ++i)
  {
    hash ^= static_cast<uint8_t>(key[i]);
    hash *= fnv_prime;
  }
  return hash;
}
}  // namespace neamc::vm
