// SPDX-License-Identifier: Apache-2.0
//
// Neam runtime runner - execute serialized bytecode bundle
//

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/memory_backend.hpp"
#include "neamc/vm/vm.hpp"

namespace
{
void usage()
{
  std::cout << "Usage: neam <bundle.neamb>\n";
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    usage();
    return 1;
  }

  const std::string input = argv[1];
  if (input == "--version" || input == "-v")
  {
    std::cout << "neam 0.6.2\n";
    return 0;
  }
  std::ifstream in_file(input, std::ios::binary);
  if (!in_file)
  {
    std::cerr << "Failed to open bundle: " << input << "\n";
    return 1;
  }

  try
  {
    neamc::vm::Chunk chunk = neamc::vm::Chunk::deserialize(in_file);
    neamc::vm::VirtualMachine vm;

    // Set up memory backend from environment
    const char* memory_backend_env = std::getenv("NEAM_MEMORY_BACKEND");
    const std::string memory_backend_type = memory_backend_env ? memory_backend_env : "memory";
    if (memory_backend_type == "sqlite")
    {
      const char* memory_path_env = std::getenv("NEAM_MEMORY_PATH");
      std::string memory_path;
      if (memory_path_env)
      {
        memory_path = memory_path_env;
      }
      else
      {
        const char* home = std::getenv("HOME");
        if (home)
        {
          memory_path = std::string(home) + "/.neam/memory.db";
        }
        else
        {
          memory_path = "neam_memory.db";
        }
      }
      vm.set_memory_backend(neamc::vm::create_memory_backend("sqlite", memory_path));
    }

    const auto result = vm.run(chunk);

    // Print emitted values
    for (const auto& emitted : vm.emitted())
    {
      if (emitted.is_string())
      {
        auto* str = neamc::vm::as_string(emitted);
        std::cout << std::string(str->chars, str->length) << "\n";
      }
      else if (emitted.is_number())
      {
        std::cout << emitted.as_number() << "\n";
      }
      else if (emitted.is_bool())
      {
        std::cout << (emitted.as_bool() ? "true" : "false") << "\n";
      }
      else if (emitted.is_nil())
      {
        std::cout << "nil\n";
      }
      else
      {
        std::cout << "<object>\n";
      }
    }

    if (result.is_number())
    {
      std::cout << "Result: " << result.as_number() << "\n";
    }
    else if (result.is_bool())
    {
      std::cout << "Result: " << (result.as_bool() ? "true" : "false") << "\n";
    }
    else if (result.is_string())
    {
      auto* str = neamc::vm::as_string(result);
      std::cout << "Result: " << std::string(str->chars, str->length) << "\n";
    }
    else if (result.is_list())
    {
      auto* list = neamc::vm::as_list(result);
      std::cout << "Result: [";
      for (std::size_t i = 0; i < list->items.size(); ++i)
      {
        const auto& item = list->items[i];
        if (item.is_string())
        {
          auto* str = neamc::vm::as_string(item);
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
        if (i + 1 < list->items.size())
        {
          std::cout << ", ";
        }
      }
      std::cout << "]\n";
    }
    else if (result.is_map())
    {
      auto* map = neamc::vm::as_map(result);
      std::cout << "Result: {";
      std::size_t count = 0;
      for (const auto& entry : map->entries)
      {
        std::cout << entry.first << ": ";
        const auto& value = entry.second;
        if (value.is_string())
        {
          auto* str = neamc::vm::as_string(value);
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
      std::cout << "}\n";
    }
    else if (result.is_agent())
    {
      auto* agent = neamc::vm::as_agent(result);
      std::cout << "Result: <agent " << std::string(agent->name->chars, agent->name->length)
                << ">\n";
    }
    else if (result.is_skill())
    {
      auto* skill = neamc::vm::as_skill(result);
      std::cout << "Result: <skill " << std::string(skill->name->chars, skill->name->length)
                << ">\n";
    }
    else if (result.is_nil())
    {
      std::cout << "Result: nil\n";
    }
    else
    {
      std::cout << "Result: <object>\n";
    }
  }
  catch (const std::exception& ex)
  {
    std::cerr << "Runtime error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
