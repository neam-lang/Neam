//
// Neam Virtual Machine - Request Metrics (v0.7.2 HotVM)
//
// Rolling-window percentile tracking for request latencies.
// Thread-safe via mutex; designed for low-overhead recording.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace neamc::vm
{

struct RequestTiming
{
  double acquire_us{0};   // VM pool acquire time
  double inject_us{0};    // Global injection time
  double execute_us{0};   // Bytecode execution time
  double total_us{0};     // End-to-end request time
  bool warm{false};       // Was this a warm VM?
  std::string agent_id;
};

struct MetricsSnapshot
{
  double p50_ms{0};
  double p95_ms{0};
  double p99_ms{0};
  uint64_t count{0};
};

class RequestMetrics
{
public:
  explicit RequestMetrics(size_t max_entries = 10000);

  // Record a request timing
  void record(const RequestTiming& timing);

  // Compute percentiles from the rolling window
  MetricsSnapshot compute() const;

  // Total request count (including evicted entries)
  uint64_t total_count() const;

private:
  mutable std::mutex mutex_;
  std::deque<double> latencies_;  // total_us values in microseconds
  size_t max_entries_;
  uint64_t total_count_{0};
};

}  // namespace neamc::vm
