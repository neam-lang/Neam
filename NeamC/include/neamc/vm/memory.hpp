//
// Neam Virtual Machine - Memory management (Garbage Collector)
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <new>

#include "neamc/vm/value.hpp"

namespace neamc::vm
{
class VirtualMachine;
struct Obj;
struct ObjString;
struct ObjFunction;
struct ObjNative;
class Table;

extern std::size_t bytes_allocated;
extern std::size_t next_gc;
extern Obj* objects;
extern VirtualMachine* current_vm;

void* reallocate(void* pointer, std::size_t old_size, std::size_t new_size);
void mark_object(Obj* object);
void mark_value(const Value& value);
void collect_garbage(VirtualMachine& vm);
void free_objects();

template <typename T>
T* allocate_object(ObjType type)
{
  static_assert(std::is_base_of_v<Obj, T>, "T must derive from Obj");
  auto* memory = static_cast<T*>(reallocate(nullptr, 0, sizeof(T)));
  T* object = new (memory) T();
  object->type = type;
  object->marked = false;
  object->next = objects;
  objects = object;
  return object;
}

#define ALLOCATE(type, count) static_cast<type*>(reallocate(nullptr, 0, sizeof(type) * (count)))
}  // namespace neamc::vm
