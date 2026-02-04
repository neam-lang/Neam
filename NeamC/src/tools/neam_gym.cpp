// SPDX-License-Identifier: Apache-2.0
//
// Neam Gym - evaluation harness
//

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/eval/graders.hpp"
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

struct SingleEvalMetrics
{
  bool passed{false};
  double latency_ms{0.0};
  std::size_t prompt_tokens{0};
  std::size_t completion_tokens{0};
  double cost_usd{0.0};
  std::string explanation;
};

void usage()
{
  std::cout << "Usage: neam-gym --agent <bundle.neamb> --dataset <workout.jsonl> [options]\n"
            << "\nOptions:\n"
            << "  --output <report.json>       Output report file (default: report.json)\n"
            << "  --runs N                     Number of evaluation runs (default: 1)\n"
            << "  --judge-provider <provider>  LLM provider for llm_judge grader (default: openai)\n"
            << "  --judge-model <model>        LLM model for llm_judge grader (default: gpt-4o-mini)\n"
            << "  --semantic-threshold <val>   Threshold for semantic_match grader (default: 0.8)\n";
}

std::string strip_trailing_newlines(std::string value)
{
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
  {
    value.pop_back();
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

SingleEvalMetrics run_single_eval(const neamc::vm::Chunk& chunk, const WorkoutRow& row,
                                   const neamc::eval::GraderConfig& grader_config)
{
  SingleEvalMetrics metrics;

  std::istringstream input_stream(row.input);
  std::ostringstream output_stream;
  neamc::vm::VirtualMachine vm;

  const auto start = std::chrono::steady_clock::now();
  neamc::vm::Value result = vm.run(chunk, &input_stream, &output_stream);
  const auto end = std::chrono::steady_clock::now();

  metrics.latency_ms =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

  // Extract tokens and cost from trace logger
  metrics.prompt_tokens = vm.trace().total_prompt_tokens();
  metrics.completion_tokens = vm.trace().total_completion_tokens();
  metrics.cost_usd = vm.trace().total_cost_usd();

  std::string actual_output = strip_trailing_newlines(output_stream.str());
  if (actual_output.empty() && !result.is_nil())
  {
    actual_output = render_value(result);
  }

  // Use new grading framework
  auto grade_result = neamc::eval::grade(row.expected, actual_output, row.grader, grader_config);
  metrics.passed = grade_result.passed;
  metrics.explanation = grade_result.explanation;

  // Add judge token usage if applicable
  metrics.prompt_tokens += grade_result.usage.prompt_tokens;
  metrics.completion_tokens += grade_result.usage.completion_tokens;
  metrics.cost_usd += 0.0;  // Judge cost tracked separately

  return metrics;
}

double compute_mean(const std::vector<double>& values)
{
  if (values.empty()) return 0.0;
  return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double compute_stddev(const std::vector<double>& values, double mean)
{
  if (values.size() <= 1) return 0.0;
  double sum_sq = 0.0;
  for (double v : values)
  {
    sum_sq += (v - mean) * (v - mean);
  }
  return std::sqrt(sum_sq / static_cast<double>(values.size() - 1));
}

double compute_percentile(std::vector<double>& values, double percentile)
{
  if (values.empty()) return 0.0;
  std::size_t n = static_cast<std::size_t>(percentile / 100.0 * static_cast<double>(values.size() - 1));
  if (n >= values.size()) n = values.size() - 1;
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(n), values.end());
  return values[n];
}

}  // namespace

int main(int argc, char** argv)
{
  std::string agent_path;
  std::string dataset_path;
  std::string output_path = "report.json";
  std::size_t num_runs = 1;
  neamc::eval::GraderConfig grader_config;

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
    else if (arg == "--runs" && i + 1 < argc)
    {
      num_runs = static_cast<std::size_t>(std::stoi(argv[++i]));
      if (num_runs == 0) num_runs = 1;
    }
    else if (arg == "--judge-provider" && i + 1 < argc)
    {
      grader_config.judge_provider = argv[++i];
    }
    else if (arg == "--judge-model" && i + 1 < argc)
    {
      grader_config.judge_model = argv[++i];
    }
    else if (arg == "--semantic-threshold" && i + 1 < argc)
    {
      grader_config.semantic_threshold = std::stod(argv[++i]);
    }
    else if (arg == "--help" || arg == "-h")
    {
      usage();
      return 0;
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

  neamc::vm::Chunk chunk = neamc::vm::Chunk::deserialize(bundle);

  // Load dataset rows
  std::vector<WorkoutRow> rows;
  {
    std::ifstream dataset(dataset_path);
    if (!dataset)
    {
      std::cerr << "Failed to open dataset: " << dataset_path << "\n";
      return 1;
    }
    std::string line;
    while (std::getline(dataset, line))
    {
      if (line.find_first_not_of(" \t\r\n") == std::string::npos)
      {
        continue;
      }
      if (auto row_opt = parse_row(line))
      {
        rows.push_back(std::move(*row_opt));
      }
    }
  }

  if (rows.empty())
  {
    std::cerr << "Dataset is empty\n";
    return 1;
  }

  // Multi-run evaluation
  std::vector<double> run_pass_rates;
  std::vector<double> run_avg_latencies;
  std::vector<double> run_total_tokens;
  std::vector<double> run_total_costs;
  std::vector<double> all_latencies;  // All individual latencies across all runs

  for (std::size_t run = 0; run < num_runs; ++run)
  {
    std::size_t passed = 0;
    double run_latency_sum = 0.0;
    std::size_t run_tokens = 0;
    double run_cost = 0.0;

    for (const auto& row : rows)
    {
      auto metrics = run_single_eval(chunk, row, grader_config);
      if (metrics.passed)
      {
        ++passed;
      }
      run_latency_sum += metrics.latency_ms;
      all_latencies.push_back(metrics.latency_ms);
      run_tokens += metrics.prompt_tokens + metrics.completion_tokens;
      run_cost += metrics.cost_usd;
    }

    double pass_rate = static_cast<double>(passed) / static_cast<double>(rows.size());
    double avg_latency = run_latency_sum / static_cast<double>(rows.size());

    run_pass_rates.push_back(pass_rate);
    run_avg_latencies.push_back(avg_latency);
    run_total_tokens.push_back(static_cast<double>(run_tokens));
    run_total_costs.push_back(run_cost);

    if (num_runs > 1)
    {
      std::cout << "Run " << (run + 1) << "/" << num_runs << ": "
                << passed << "/" << rows.size() << " passed\n";
    }
  }

  // Compute statistics
  double pass_rate_mean = compute_mean(run_pass_rates);
  double pass_rate_stddev = compute_stddev(run_pass_rates, pass_rate_mean);
  double latency_mean = compute_mean(run_avg_latencies);
  double latency_stddev = compute_stddev(run_avg_latencies, latency_mean);
  double tokens_mean = compute_mean(run_total_tokens);
  double tokens_stddev = compute_stddev(run_total_tokens, tokens_mean);
  double cost_mean = compute_mean(run_total_costs);
  double cost_stddev = compute_stddev(run_total_costs, cost_mean);

  // Compute latency percentiles from all individual latencies
  double p50 = compute_percentile(all_latencies, 50.0);
  double p95 = compute_percentile(all_latencies, 95.0);
  double p99 = compute_percentile(all_latencies, 99.0);

  // Build report
  nlohmann::json report;
  report["total_reps"] = rows.size() * num_runs;
  report["num_runs"] = num_runs;
  report["pass_rate"] = {{"mean", pass_rate_mean}, {"stddev", pass_rate_stddev}};
  report["avg_latency_ms"] = {{"mean", latency_mean}, {"stddev", latency_stddev}};
  report["latency_percentiles"] = {{"p50", p50}, {"p95", p95}, {"p99", p99}};
  report["total_tokens"] = {{"mean", tokens_mean}, {"stddev", tokens_stddev}};
  report["total_cost_usd"] = {{"mean", cost_mean}, {"stddev", cost_stddev}};

  std::ofstream report_out(output_path);
  if (!report_out)
  {
    std::cerr << "Failed to write report to " << output_path << "\n";
    return 1;
  }
  report_out << report.dump(2) << "\n";

  std::size_t total_passed = static_cast<std::size_t>(pass_rate_mean * static_cast<double>(rows.size()));
  std::cout << "Gym run complete. Passed " << total_passed << "/" << rows.size();
  if (num_runs > 1)
  {
    std::cout << " (mean across " << num_runs << " runs, stddev=" << pass_rate_stddev << ")";
  }
  std::cout << ".\n";
  std::cout << "Report written to " << output_path << "\n";

  return 0;
}
