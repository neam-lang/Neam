//
// Neam Virtual Machine - Mark-Sweep Garbage Collector
//

#include "neamc/vm/memory.hpp"

#include <cstdlib>
#include <vector>

#include "neamc/vm/object.hpp"
#include "neamc/vm/table.hpp"
#include "neamc/vm/vm.hpp"

namespace neamc::vm
{
std::size_t bytes_allocated = 0;
std::size_t next_gc = 1024 * 1024;
Obj* objects = nullptr;
VirtualMachine* current_vm = nullptr;

namespace
{
void free_object(Obj* object)
{
  switch (object->type)
  {
    case ObjType::OBJ_STRING:
    {
      auto* string = reinterpret_cast<ObjString*>(object);
      std::free(string->chars);
      string->~ObjString();
      std::free(string);
      break;
    }
    case ObjType::OBJ_FUNCTION:
    {
      auto* fn = reinterpret_cast<ObjFunction*>(object);
      fn->~ObjFunction();
      std::free(fn);
      break;
    }
    case ObjType::OBJ_NATIVE:
    {
      object->~Obj();
      std::free(object);
      break;
    }
  }
}

void mark_object_inner(Obj* object, std::vector<Obj*>& gray_stack)
{
  if (object == nullptr || object->marked)
  {
    return;
  }
  object->marked = true;
  gray_stack.push_back(object);
}

void blacken_object(Obj* object, std::vector<Obj*>& gray_stack)
{
  switch (object->type)
  {
    case ObjType::OBJ_STRING:
      break;
    case ObjType::OBJ_FUNCTION:
    {
      auto* function = reinterpret_cast<ObjFunction*>(object);
      if (function->name)
      {
        mark_object_inner(reinterpret_cast<Obj*>(function->name), gray_stack);
      }
      for (const auto& constant : function->chunk.constants())
      {
        mark_value(const_cast<Value&>(constant));
      }
      break;
    }
    case ObjType::OBJ_NATIVE:
    {
      auto* native = reinterpret_cast<ObjNative*>(object);
      if (native->name)
      {
        mark_object_inner(reinterpret_cast<Obj*>(native->name), gray_stack);
      }
      break;
    }
  }
}
}  // namespace

void* reallocate(void* pointer, std::size_t old_size, std::size_t new_size)
{
  bytes_allocated += new_size;
  if (old_size > 0)
  {
    bytes_allocated -= old_size;
  }
  if (new_size > old_size && bytes_allocated > next_gc && current_vm)
  {
    collect_garbage(*current_vm);
  }
  if (new_size == 0)
  {
    std::free(pointer);
    return nullptr;
  }
  void* result = std::realloc(pointer, new_size);
  if (!result)
  {
    std::abort();
  }
  return result;
}

void mark_object(Obj* object)
{
  static std::vector<Obj*> gray_stack;
  gray_stack.clear();
  mark_object_inner(object, gray_stack);
  while (!gray_stack.empty())
  {
    Obj* obj = gray_stack.back();
    gray_stack.pop_back();
    blacken_object(obj, gray_stack);
  }
}

void mark_value(const Value& value)
{
  if (value.is_obj())
  {
    mark_object(const_cast<Obj*>(value.as_obj()));
  }
}

void collect_garbage(VirtualMachine& vm)
{
  std::vector<Obj*> gray_stack;
  for (const auto& value : vm.stack())
  {
    mark_object_inner(value.is_obj() ? value.as.obj : nullptr, gray_stack);
  }
  for (const auto& frame : vm.frames())
  {
    if (frame.function && frame.function->name)
    {
      mark_object_inner(reinterpret_cast<Obj*>(frame.function->name), gray_stack);
    }
    for (const auto& constant : frame.chunk->constants())
    {
      mark_value(constant);
    }
  }
  mark_table(vm.globals());
  mark_table(vm.strings());

  while (!gray_stack.empty())
  {
    Obj* obj = gray_stack.back();
    gray_stack.pop_back();
    blacken_object(obj, gray_stack);
  }

  Obj* previous = nullptr;
  Obj* current = objects;
  while (current)
  {
    if (current->marked)
    {
      current->marked = false;
      previous = current;
      current = current->next;
    }
    else
    {
      Obj* unreached = current;
      current = current->next;
      if (previous)
      {
        previous->next = current;
      }
      else
      {
        objects = current;
      }
      free_object(unreached);
    }
  }
}
}  // namespace neamc::vm
