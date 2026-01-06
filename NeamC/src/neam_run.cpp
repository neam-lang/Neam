//
// Neam runtime runner - execute serialized bytecode bundle
//

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "neamc/vm/bytecode.hpp"
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
    const auto result = vm.run(chunk);

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
      std::cout << "Result: " << result.as_string() << "\n";
    }
    else if (result.is_nil())
    {
      std::cout << "Result: nil\n";
    }
    else
    {
      std::cout << "Result: <agent>\n";
    }
  }
  catch (const std::exception& ex)
  {
    std::cerr << "Runtime error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
