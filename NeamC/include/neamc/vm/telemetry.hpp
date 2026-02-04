// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - OpenTelemetry Export (v0.6.0)
//

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace neamc::vm
{

struct TelemetryConfig
{
  bool enabled{false};
  std::string endpoint;
  std::string protocol{"otlp-http"};
  double sampling_rate{1.0};
  std::string service_name{"neam-agent"};
  std::string environment;
  std::unordered_map<std::string, std::string> resource_attributes;
};

/// Lightweight span for structured tracing
struct Span
{
  std::string trace_id;
  std::string span_id;
  std::string parent_id;
  std::string name;
  std::chrono::steady_clock::time_point start;
  std::chrono::steady_clock::time_point end;
  std::unordered_map<std::string, std::string> attributes;
  enum class Status
  {
    kOk,
    kError,
    kUnset
  } status{Status::kUnset};
  std::string status_message;
};

/// Counter metric
struct MetricPoint
{
  std::string name;
  std::string unit;
  double value{0.0};
  std::unordered_map<std::string, std::string> labels;
  std::chrono::system_clock::time_point timestamp;
};

class TelemetryExporter
{
public:
  explicit TelemetryExporter(const TelemetryConfig& config);
  ~TelemetryExporter();

  TelemetryExporter(const TelemetryExporter&) = delete;
  TelemetryExporter& operator=(const TelemetryExporter&) = delete;

  /// Start a new span (returns span ID for child spans)
  std::string start_span(const std::string& name,
                         const std::string& parent_span_id = "",
                         const std::unordered_map<std::string, std::string>& attributes = {});

  /// End a span (triggers buffered export)
  void end_span(const std::string& span_id,
                Span::Status status = Span::Status::kOk,
                const std::string& status_message = "");

  /// Add attributes to an active span
  void set_attribute(const std::string& span_id,
                     const std::string& key, const std::string& value);

  /// Record a metric point
  void record_metric(const MetricPoint& point);

  /// Flush pending spans and metrics to OTLP endpoint
  void flush();

  /// Check if tracing is enabled (respects sampling rate)
  bool should_sample() const;

private:
  void export_loop();
  void export_spans(const std::vector<Span>& spans);
  void export_metrics(const std::vector<MetricPoint>& metrics);
  std::string generate_id(size_t bytes) const;

  TelemetryConfig config_;
  mutable std::mutex mutex_;
  std::vector<Span> pending_spans_;
  std::vector<MetricPoint> pending_metrics_;
  std::unordered_map<std::string, Span> active_spans_;
  std::string instance_trace_id_;

  // Background export thread
  std::atomic<bool> running_{false};
  std::thread export_thread_;
  std::condition_variable export_cv_;
};

}  // namespace neamc::vm
