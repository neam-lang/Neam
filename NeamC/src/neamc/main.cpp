//
// Minimal NeamC CLI stub with VM flag
//

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
  std::vector<std::string> args(argv + 1, argv + argc);

  for (const auto& arg : args)
  {
    if (arg == "--help" || arg == "-h")
    {
      std::cout << "NeamC (stub)\n"
                << "Usage: neamc [--vm]\n"
                << "  --vm   Run the Neam Virtual Machine (future extension)\n";
      return 0;
    }
    if (arg == "--vm")
    {
      std::cout << "NeamC VM flag detected. VM execution entry points will be added in a "
                   "future phase.\n";
      return 0;
    }
  }

  std::cout << "NeamC compiler stub. Use --help for options.\n";
  return 0;
}
