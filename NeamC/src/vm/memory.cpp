// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Mark-Sweep Garbage Collector
//

#include "neamc/vm/memory.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "neamc/vm/object.hpp"
#include "neamc/vm/async/future.hpp"
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
  if (object == nullptr) return;

  // Detach from linked list before freeing to prevent use-after-free
  object->next = nullptr;

  switch (object->type)
  {
    case ObjType::OBJ_STRING:
    {
      auto* string = reinterpret_cast<ObjString*>(object);
      char* chars = string->chars;
      string->chars = nullptr;
      std::free(chars);
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
    case ObjType::OBJ_LIST:
    {
      auto* list = reinterpret_cast<ObjList*>(object);
      list->~ObjList();
      std::free(list);
      break;
    }
    case ObjType::OBJ_MAP:
    {
      auto* map = reinterpret_cast<ObjMap*>(object);
      map->~ObjMap();
      std::free(map);
      break;
    }
    case ObjType::OBJ_SKILL:
    {
      auto* skill = reinterpret_cast<ObjSkill*>(object);
      skill->~ObjSkill();
      std::free(skill);
      break;
    }
    case ObjType::OBJ_AGENT:
    {
      auto* agent = reinterpret_cast<ObjAgent*>(object);
      agent->~ObjAgent();
      std::free(agent);
      break;
    }
    case ObjType::OBJ_CONTEXT:
    {
      auto* context = reinterpret_cast<ObjContext*>(object);
      context->~ObjContext();
      std::free(context);
      break;
    }
    case ObjType::OBJ_ENV:
    {
      auto* env = reinterpret_cast<ObjEnv*>(object);
      env->~ObjEnv();
      std::free(env);
      break;
    }
    case ObjType::OBJ_KNOWLEDGE:
    {
      auto* knowledge = reinterpret_cast<ObjKnowledge*>(object);
      knowledge->~ObjKnowledge();
      std::free(knowledge);
      break;
    }
    case ObjType::OBJ_OPTION:
    {
      auto* option = reinterpret_cast<ObjOption*>(object);
      option->~ObjOption();
      std::free(option);
      break;
    }
    case ObjType::OBJ_FUTURE:
    {
      auto* future = reinterpret_cast<ObjFuture*>(object);
      future->~ObjFuture();
      std::free(future);
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
    case ObjType::OBJ_LIST:
    {
      auto* list = reinterpret_cast<ObjList*>(object);
      for (const auto& item : list->items)
      {
        mark_value(const_cast<Value&>(item));
      }
      break;
    }
    case ObjType::OBJ_MAP:
    {
      auto* map = reinterpret_cast<ObjMap*>(object);
      for (const auto& entry : map->entries)
      {
        mark_value(const_cast<Value&>(entry.second));
      }
      break;
    }
    case ObjType::OBJ_SKILL:
    {
      auto* skill = reinterpret_cast<ObjSkill*>(object);
      if (skill->name)
      {
        mark_object_inner(reinterpret_cast<Obj*>(skill->name), gray_stack);
      }
      if (skill->description)
      {
        mark_object_inner(reinterpret_cast<Obj*>(skill->description), gray_stack);
      }
      if (skill->params)
      {
        mark_object_inner(reinterpret_cast<Obj*>(skill->params), gray_stack);
      }
      if (skill->impl)
      {
        mark_object_inner(reinterpret_cast<Obj*>(skill->impl), gray_stack);
      }
      break;
    }
    case ObjType::OBJ_AGENT:
    {
      auto* agent = reinterpret_cast<ObjAgent*>(object);
      if (agent->name)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->name), gray_stack);
      }
      if (agent->provider)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->provider), gray_stack);
      }
      if (agent->model)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->model), gray_stack);
      }
      if (agent->endpoint)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->endpoint), gray_stack);
      }
      if (agent->api_key_env)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->api_key_env), gray_stack);
      }
      if (agent->system)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->system), gray_stack);
      }
      if (agent->skills)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->skills), gray_stack);
      }
      if (agent->connected_knowledge)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->connected_knowledge), gray_stack);
      }
      if (agent->context)
      {
        mark_object_inner(reinterpret_cast<Obj*>(agent->context), gray_stack);
      }
      break;
    }
    case ObjType::OBJ_CONTEXT:
      break;
    case ObjType::OBJ_ENV:
      break;
    case ObjType::OBJ_KNOWLEDGE:
    {
      auto* knowledge = reinterpret_cast<ObjKnowledge*>(object);
      if (knowledge->name)
      {
        mark_object_inner(reinterpret_cast<Obj*>(knowledge->name), gray_stack);
      }
      if (knowledge->vector_store)
      {
        mark_object_inner(reinterpret_cast<Obj*>(knowledge->vector_store), gray_stack);
      }
      if (knowledge->embedding_model)
      {
        mark_object_inner(reinterpret_cast<Obj*>(knowledge->embedding_model), gray_stack);
      }
      break;
    }
    case ObjType::OBJ_OPTION:
    {
      auto* option = reinterpret_cast<ObjOption*>(object);
      if (option->has_value)
      {
        mark_value(option->value);
      }
      break;
    }
    case ObjType::OBJ_FUTURE:
    {
      auto* future = reinterpret_cast<ObjFuture*>(object);
      if (future->future)
      {
        auto value = future->future->try_get();
        if (value.has_value())
        {
          mark_value(*value);
        }
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
  for (const auto& root : vm.roots())
  {
    mark_value(root);
  }
  for (const auto& emitted : vm.emitted())
  {
    mark_value(emitted);
  }
  mark_table(vm.globals());

  while (!gray_stack.empty())
  {
    Obj* obj = gray_stack.back();
    gray_stack.pop_back();
    blacken_object(obj, gray_stack);
  }
  table_remove_white(vm.strings());

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
  next_gc = std::max(bytes_allocated * 2, static_cast<std::size_t>(1024 * 1024));
}

void free_objects()
{
  Obj* object = objects;
  while (object)
  {
    Obj* next = object->next;
    free_object(object);
    object = next;
  }
  objects = nullptr;
  bytes_allocated = 0;
  next_gc = 1024 * 1024;
}
}  // namespace neamc::vm
