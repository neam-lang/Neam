//
// Neam Virtual Machine - Memory management (Garbage Collector)
//
// v0.7.2: Per-VM GC state — globals replaced with thread-local current_vm
// and per-VM object lists/counters for safe VM pooling.
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

// v0.7.2: Thread-local current VM accessors (replaces global current_vm)
VirtualMachine* get_current_vm();
void set_current_vm(VirtualMachine* vm);

void* reallocate(void* pointer, std::size_t old_size, std::size_t new_size);
void mark_object(Obj* object);
void mark_value(const Value& value);
void collect_garbage(VirtualMachine& vm);
void free_objects(VirtualMachine& vm);

// Link a newly allocated object into the current VM's object list
void link_object(Obj* object);

template <typename T>
T* allocate_object(ObjType type)
{
  static_assert(std::is_base_of_v<Obj, T>, "T must derive from Obj");
  auto* memory = static_cast<T*>(reallocate(nullptr, 0, sizeof(T)));
  T* object = new (memory) T();
  object->type = type;
  object->marked = false;
  link_object(object);
  return object;
}

#define ALLOCATE(type, count) static_cast<type*>(reallocate(nullptr, 0, sizeof(type) * (count)))
}  // namespace neamc::vm
