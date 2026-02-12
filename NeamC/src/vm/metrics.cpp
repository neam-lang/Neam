//
// Neam Virtual Machine - Request Metrics (v0.7.2 HotVM)
//

#include "neamc/vm/metrics.hpp"

#include <algorithm>
#include <vector>

namespace neamc::vm
{

RequestMetrics::RequestMetrics(size_t max_entries)
    : max_entries_(max_entries)
{
}

void RequestMetrics::record(const RequestTiming& timing)
{
  std::lock_guard<std::mutex> lock(mutex_);
  latencies_.push_back(timing.total_us);
  ++total_count_;
  while (latencies_.size() > max_entries_)
  {
    latencies_.pop_front();
  }
}

MetricsSnapshot RequestMetrics::compute() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (latencies_.empty())
  {
    return {0, 0, 0, total_count_};
  }

  // Copy and sort for percentile computation
  std::vector<double> sorted(latencies_.begin(), latencies_.end());
  std::sort(sorted.begin(), sorted.end());

  auto percentile = [&sorted](double p) -> double {
    if (sorted.empty()) return 0;
    double idx = p * static_cast<double>(sorted.size() - 1);
    auto lo = static_cast<size_t>(idx);
    double frac = idx - static_cast<double>(lo);
    if (lo + 1 < sorted.size())
    {
      return (sorted[lo] * (1.0 - frac) + sorted[lo + 1] * frac) / 1000.0;  // us -> ms
    }
    return sorted[lo] / 1000.0;
  };

  return {percentile(0.50), percentile(0.95), percentile(0.99), total_count_};
}

uint64_t RequestMetrics::total_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return total_count_;
}

}  // namespace neamc::vm
