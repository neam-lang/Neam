// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Coverage reporting implementation
//

#include "neamc/testing/coverage.hpp"

#include <fstream>

namespace neamc::testing
{
namespace
{
size_t count_executed(const std::vector<LineCoverage>& lines)
{
  size_t executed = 0;
  for (const auto& line : lines)
  {
    if (line.execution_count > 0)
    {
      executed += 1;
    }
  }
  return executed;
}

size_t count_executed(const std::vector<FunctionCoverage>& funcs)
{
  size_t executed = 0;
  for (const auto& func : funcs)
  {
    if (func.execution_count > 0)
    {
      executed += 1;
    }
  }
  return executed;
}

}  // namespace

double FileCoverage::line_percentage() const
{
  if (lines.empty())
  {
    return 100.0;
  }
  return 100.0 * static_cast<double>(count_executed(lines)) / static_cast<double>(lines.size());
}

double FileCoverage::function_percentage() const
{
  if (functions.empty())
  {
    return 100.0;
  }
  return 100.0 * static_cast<double>(count_executed(functions)) /
         static_cast<double>(functions.size());
}

double CoverageReport::total_line_coverage() const
{
  size_t total_lines = 0;
  size_t executed = 0;
  for (const auto& file : files)
  {
    total_lines += file.lines.size();
    executed += count_executed(file.lines);
  }
  if (total_lines == 0)
  {
    return 100.0;
  }
  return 100.0 * static_cast<double>(executed) / static_cast<double>(total_lines);
}

double CoverageReport::total_function_coverage() const
{
  size_t total_funcs = 0;
  size_t executed = 0;
  for (const auto& file : files)
  {
    total_funcs += file.functions.size();
    executed += count_executed(file.functions);
  }
  if (total_funcs == 0)
  {
    return 100.0;
  }
  return 100.0 * static_cast<double>(executed) / static_cast<double>(total_funcs);
}

void CoverageReport::export_lcov(const std::string& path) const
{
  std::ofstream file(path);
  for (const auto& fc : files)
  {
    file << "SF:" << fc.file_path << "\n";
    for (const auto& line : fc.lines)
    {
      file << "DA:" << line.line_number << "," << line.execution_count << "\n";
    }
    file << "end_of_record\n";
  }
}

void CoverageReport::export_html(const std::string& output_dir) const
{
  std::ofstream file(output_dir + "/index.html");
  file << "<html><body><h1>Coverage Report</h1>";
  for (const auto& fc : files)
  {
    file << "<h2>" << fc.file_path << "</h2>";
    file << "<p>Line coverage: " << fc.line_percentage() << "%</p>";
    file << "<p>Function coverage: " << fc.function_percentage() << "%</p>";
  }
  file << "</body></html>";
}

void CoverageReport::export_json(const std::string& path) const
{
  std::ofstream file(path);
  file << "{\"files\":[";
  for (size_t i = 0; i < files.size(); ++i)
  {
    const auto& fc = files[i];
    file << "{\"path\":\"" << fc.file_path << "\",\"line_percentage\":"
         << fc.line_percentage() << ",\"function_percentage\":" << fc.function_percentage() << "}";
    if (i + 1 < files.size())
    {
      file << ",";
    }
  }
  file << "]}";
}

void CoverageCollector::instrument_file(const std::string& path)
{
  if (!coverage_data_.count(path))
  {
    coverage_data_[path] = FileCoverage{path, {}, {}};
  }
}

void CoverageCollector::record_line(const std::string& file, size_t line)
{
  auto& entry = coverage_data_[file];
  entry.file_path = file;
  for (auto& line_entry : entry.lines)
  {
    if (line_entry.line_number == line)
    {
      line_entry.execution_count += 1;
      return;
    }
  }
  entry.lines.push_back(LineCoverage{line, 1});
}

void CoverageCollector::record_function(const std::string& file, const std::string& func)
{
  auto& entry = coverage_data_[file];
  entry.file_path = file;
  for (auto& func_entry : entry.functions)
  {
    if (func_entry.name == func)
    {
      func_entry.execution_count += 1;
      return;
    }
  }
  entry.functions.push_back(FunctionCoverage{func, 0, 0, 1});
}

CoverageReport CoverageCollector::generate_report() const
{
  CoverageReport report;
  report.timestamp = std::chrono::system_clock::now();
  for (const auto& [path, file] : coverage_data_)
  {
    (void)path;
    report.files.push_back(file);
  }
  return report;
}

void CoverageCollector::reset()
{
  coverage_data_.clear();
}

CoverageCollector& coverage_collector()
{
  static CoverageCollector collector;
  return collector;
}

}  // namespace neamc::testing
