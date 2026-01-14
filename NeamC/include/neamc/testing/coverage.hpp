//
// NeamC - Coverage reporting
//

#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace neamc::testing
{
struct LineCoverage
{
  size_t line_number{0};
  size_t execution_count{0};
};

struct FunctionCoverage
{
  std::string name;
  size_t start_line{0};
  size_t end_line{0};
  size_t execution_count{0};
};

struct FileCoverage
{
  std::string file_path;
  std::vector<LineCoverage> lines;
  std::vector<FunctionCoverage> functions;

  double line_percentage() const;
  double function_percentage() const;
};

struct CoverageReport
{
  std::vector<FileCoverage> files;
  std::chrono::system_clock::time_point timestamp;

  double total_line_coverage() const;
  double total_function_coverage() const;

  void export_lcov(const std::string& path) const;
  void export_html(const std::string& output_dir) const;
  void export_json(const std::string& path) const;
};

class CoverageCollector
{
public:
  void instrument_file(const std::string& path);
  void record_line(const std::string& file, size_t line);
  void record_function(const std::string& file, const std::string& func);

  CoverageReport generate_report() const;
  void reset();

private:
  std::map<std::string, FileCoverage> coverage_data_;
};

CoverageCollector& coverage_collector();

}  // namespace neamc::testing
