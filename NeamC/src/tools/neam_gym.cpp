//
// Neam Gym - evaluation harness
//

#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/vm.hpp"

namespace
{
struct WorkoutRow
{
  std::string id;
  std::string input;
  std::string expected;
  std::string grader;
};

void usage()
{
  std::cout << "Usage: neam-gym --agent <bundle.neamb> --dataset <workout.jsonl> [--output <report.json>]\n";
}

std::string strip_trailing_newlines(std::string value)
{
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
  {
    value.pop_back();
  }
  return value;
}

std::string normalize_case(std::string value)
{
  for (char& c : value)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

std::string render_value(const neamc::vm::Value& value)
{
  std::ostringstream out;
  if (value.is_number())
  {
    out << value.as_number();
  }
  else if (value.is_bool())
  {
    out << (value.as_bool() ? "true" : "false");
  }
  else if (value.is_string())
  {
    auto* str = neamc::vm::as_string(value);
    out << std::string(str->chars, str->length);
  }
  else if (value.is_list())
  {
    auto* list = neamc::vm::as_list(value);
    out << "[";
    for (std::size_t i = 0; i < list->items.size(); ++i)
    {
      if (i)
      {
        out << ", ";
      }
      out << render_value(list->items[i]);
    }
    out << "]";
  }
  else if (value.is_map())
  {
    auto* map = neamc::vm::as_map(value);
    out << "{";
    std::size_t count = 0;
    for (const auto& entry : map->entries)
    {
      if (count++)
      {
        out << ", ";
      }
      out << entry.first << ": " << render_value(entry.second);
    }
    out << "}";
  }
  else if (value.is_agent())
  {
    auto* agent = neamc::vm::as_agent(value);
    out << "<agent " << std::string(agent->name->chars, agent->name->length) << ">";
  }
  else if (value.is_skill())
  {
    auto* skill = neamc::vm::as_skill(value);
    out << "<skill " << std::string(skill->name->chars, skill->name->length) << ">";
  }
  else if (value.is_nil())
  {
    out << "nil";
  }
  else
  {
    out << "<object>";
  }
  return out.str();
}

bool grade_exact_match(const std::string& expected, const std::string& actual)
{
  return normalize_case(expected) == normalize_case(actual);
}

bool grade_contains(const std::string& expected, const std::string& actual)
{
  return actual.find(expected) != std::string::npos;
}

bool grade_regex(const std::string& expected, const std::string& actual)
{
  try
  {
    const std::regex pattern(expected);
    return std::regex_search(actual, pattern);
  }
  catch (const std::regex_error&)
  {
    return false;
  }
}

bool grade_output(const std::string& expected, const std::string& actual,
                  const std::string& grader)
{
  if (grader == "exact_match")
  {
    return grade_exact_match(expected, actual);
  }
  if (grader == "contains")
  {
    return grade_contains(expected, actual);
  }
  if (grader == "regex")
  {
    return grade_regex(expected, actual);
  }
  return false;
}

std::optional<WorkoutRow> parse_row(const std::string& line)
{
  if (line.empty())
  {
    return std::nullopt;
  }
  const auto json = nlohmann::json::parse(line);
  WorkoutRow row;
  row.id = json.value("id", "");
  row.input = json.value("input", "");
  row.expected = json.value("expected", "");
  row.grader = json.value("grader", "exact_match");
  if (row.id.empty())
  {
    row.id = "rep";
  }
  return row;
}
}  // namespace

int main(int argc, char** argv)
{
  std::string agent_path;
  std::string dataset_path;
  std::string output_path = "report.json";

  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    if (arg == "--agent" && i + 1 < argc)
    {
      agent_path = argv[++i];
    }
    else if (arg == "--dataset" && i + 1 < argc)
    {
      dataset_path = argv[++i];
    }
    else if (arg == "--output" && i + 1 < argc)
    {
      output_path = argv[++i];
    }
    else
    {
      usage();
      return 1;
    }
  }

  if (agent_path.empty() || dataset_path.empty())
  {
    usage();
    return 1;
  }

  std::ifstream bundle(agent_path, std::ios::binary);
  if (!bundle)
  {
    std::cerr << "Failed to open agent bundle: " << agent_path << "\n";
    return 1;
  }

  std::ifstream dataset(dataset_path);
  if (!dataset)
  {
    std::cerr << "Failed to open dataset: " << dataset_path << "\n";
    return 1;
  }

  neamc::vm::Chunk chunk = neamc::vm::Chunk::deserialize(bundle);

  std::size_t total = 0;
  std::size_t passed = 0;
  double total_latency_ms = 0.0;

  std::string line;
  while (std::getline(dataset, line))
  {
    if (line.find_first_not_of(" \t\r\n") == std::string::npos)
    {
      continue;
    }
    const auto row_opt = parse_row(line);
    if (!row_opt)
    {
      continue;
    }
    const auto& row = *row_opt;
    ++total;

    std::istringstream input_stream(row.input);
    std::ostringstream output_stream;
    neamc::vm::VirtualMachine vm;
    const auto start = std::chrono::steady_clock::now();
    neamc::vm::Value result = vm.run(chunk, &input_stream, &output_stream);
    const auto end = std::chrono::steady_clock::now();
    const auto latency_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    total_latency_ms += static_cast<double>(latency_ms);

    std::string actual_output = strip_trailing_newlines(output_stream.str());
    if (actual_output.empty() && !result.is_nil())
    {
      actual_output = render_value(result);
    }
    const bool ok = grade_output(row.expected, actual_output, row.grader);
    if (ok)
    {
      ++passed;
    }
  }

  const double pass_rate = total == 0 ? 0.0 : static_cast<double>(passed) / total;
  const double avg_latency_ms = total == 0 ? 0.0 : total_latency_ms / total;

  nlohmann::json report;
  report["total_reps"] = total;
  report["pass_rate"] = pass_rate;
  report["avg_latency_ms"] = avg_latency_ms;
  report["total_cost_usd"] = 0.0;

  std::ofstream report_out(output_path);
  if (!report_out)
  {
    std::cerr << "Failed to write report to " << output_path << "\n";
    return 1;
  }
  report_out << report.dump(2) << "\n";

  std::cout << "Gym run complete. Passed " << passed << "/" << total << ".\n";
  std::cout << "Report written to " << output_path << "\n";

  return 0;
}
