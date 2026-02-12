//
// Neam runtime runner - execute serialized bytecode bundle
//

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/value.hpp"
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

    // Print emitted values
    for (const auto& emitted : vm.emitted())
    {
      std::cout << neamc::vm::value_to_string(emitted) << "\n";
    }

    if (!result.is_nil())
    {
      std::cout << "Result: " << neamc::vm::value_to_string(result) << "\n";
    }
  }
  catch (const std::exception& ex)
  {
    std::cerr << "Runtime error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
