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
Value print_native(int arg_count, Value* args)
{
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
    if (i + 1 < arg_count)
    {
      std::cout << " ";
    }
  }
  std::cout << std::endl;
  return Value::Nil();
}

Value clock_native(int, Value*)
{
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
