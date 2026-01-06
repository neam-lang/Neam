//
// NeamC CLI - Compile Neam source to bytecode bundle
//

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "neamc/pipeline.hpp"

namespace
{
void usage()
{
  std::cout << "Usage: neamc <input.neam> [-o output.neamb] [--manifest <json>]\n";
}
}  // namespace

int main(int argc, char** argv)
{
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty())
  {
    usage();
    return 1;
  }

  std::string input;
  std::string output;
  std::string manifest;

  for (std::size_t i = 0; i < args.size(); ++i)
  {
    const auto& arg = args[i];
    if (arg == "--help" || arg == "-h")
    {
      usage();
      return 0;
    }
    else if (arg == "-o")
    {
      if (i + 1 >= args.size())
      {
        std::cerr << "Missing output file after -o\n";
        return 1;
      }
      output = args[++i];
    }
    else if (arg == "--manifest")
    {
      if (i + 1 >= args.size())
      {
        std::cerr << "Missing manifest string after --manifest\n";
        return 1;
      }
      manifest = args[++i];
    }
    else if (input.empty())
    {
      input = arg;
    }
    else
    {
      std::cerr << "Unexpected argument: " << arg << "\n";
      return 1;
    }
  }

  if (input.empty())
  {
    usage();
    return 1;
  }

  if (output.empty())
  {
    output = input + "b";  // append 'b' to produce .neamb by default
  }

  std::ifstream in_file(input);
  if (!in_file)
  {
    std::cerr << "Failed to open input file: " << input << "\n";
    return 1;
  }

  std::string source((std::istreambuf_iterator<char>(in_file)), std::istreambuf_iterator<char>());

  try
  {
    neamc::Pipeline pipeline;
    auto unit = pipeline.compile(source, manifest);

    std::ofstream out_file(output, std::ios::binary);
    if (!out_file)
    {
      std::cerr << "Failed to open output file: " << output << "\n";
      return 1;
    }

    unit.chunk.serialize(out_file);
    std::cout << "Wrote bytecode bundle to " << output << "\n";
  }
  catch (const std::exception& ex)
  {
    std::cerr << "Compilation failed: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
