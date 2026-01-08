//
// Neam Virtual Machine - Native function registry
//

#include "neamc/vm/native.hpp"

#include <chrono>
#include <iostream>

#include "neamc/vm/table.hpp"
#include "neamc/vm/vm.hpp"

namespace neamc::vm
{
namespace
{
Value print_native(VirtualMachine& vm, int arg_count, Value* args)
{
  (void)vm;
  for (int i = 0; i < arg_count; ++i)
  {
    const auto& arg = args[i];
    if (is_obj_type(arg, ObjType::OBJ_STRING))
    {
      std::cout.write(as_string(arg)->chars, static_cast<std::streamsize>(as_string(arg)->length));
    }
    else if (arg.is_number())
    {
      std::cout << arg.as_number();
    }
    else if (arg.is_bool())
    {
      std::cout << (arg.as_bool() ? "true" : "false");
    }
    else if (arg.is_nil())
    {
      std::cout << "nil";
    }
    else if (arg.is_list())
    {
      const auto* list = as_list(arg);
      std::cout << "[";
      for (std::size_t j = 0; j < list->items.size(); ++j)
      {
        const auto& item = list->items[j];
        if (item.is_string())
        {
          auto* str = as_string(item);
          std::cout << std::string(str->chars, str->length);
        }
        else if (item.is_number())
        {
          std::cout << item.as_number();
        }
        else if (item.is_bool())
        {
          std::cout << (item.as_bool() ? "true" : "false");
        }
        else if (item.is_nil())
        {
          std::cout << "nil";
        }
        else
        {
          std::cout << "<object>";
        }
        if (j + 1 < list->items.size())
        {
          std::cout << ", ";
        }
      }
      std::cout << "]";
    }
    else if (arg.is_map())
    {
      const auto* map = as_map(arg);
      std::cout << "{";
      std::size_t count = 0;
      for (const auto& entry : map->entries)
      {
        std::cout << entry.first << ": ";
        const auto& value = entry.second;
        if (value.is_string())
        {
          auto* str = as_string(value);
          std::cout << std::string(str->chars, str->length);
        }
        else if (value.is_number())
        {
          std::cout << value.as_number();
        }
        else if (value.is_bool())
        {
          std::cout << (value.as_bool() ? "true" : "false");
        }
        else if (value.is_nil())
        {
          std::cout << "nil";
        }
        else
        {
          std::cout << "<object>";
        }
        if (++count < map->entries.size())
        {
          std::cout << ", ";
        }
      }
      std::cout << "}";
    }
    else if (arg.is_agent())
    {
      auto* agent = as_agent(arg);
      auto* name = agent->name;
      std::cout << "<agent " << std::string(name->chars, name->length) << ">";
    }
    else if (arg.is_skill())
    {
      auto* skill = as_skill(arg);
      auto* name = skill->name;
      std::cout << "<skill " << std::string(name->chars, name->length) << ">";
    }
    if (i + 1 < arg_count)
    {
      std::cout << " ";
    }
  }
  std::cout << std::endl;
  return Value::Nil();
}

Value clock_native(VirtualMachine& vm, int, Value*)
{
  (void)vm;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return Value::Number(static_cast<double>(ms) / 1000.0);
}
}  // namespace

void register_core_natives(VirtualMachine& vm)
{
  vm.define_native("print", -1, print_native);
  vm.define_native("clock", 0, clock_native);
}
}  // namespace neamc::vm
