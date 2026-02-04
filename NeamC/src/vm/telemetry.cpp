// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - OpenTelemetry Export (v0.6.0)
//

#include "neamc/vm/telemetry.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>

namespace neamc::vm
{
namespace
{
size_t discard_write(char* /*ptr*/, size_t size, size_t nmemb, void* /*userdata*/)
{
  return size * nmemb;
}

std::string to_hex(const unsigned char* data, size_t len)
{
  std::ostringstream oss;
  for (size_t i = 0; i < len; ++i)
  {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]);
  }
  return oss.str();
}

int64_t steady_to_unix_nano(std::chrono::steady_clock::time_point tp)
{
  // Convert steady_clock to system_clock approximately
  auto now_steady = std::chrono::steady_clock::now();
  auto now_system = std::chrono::system_clock::now();
  auto offset = tp - now_steady;
  auto system_tp = now_system + offset;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(system_tp.time_since_epoch()).count();
}

nlohmann::json make_attribute(const std::string& key, const std::string& value)
{
  return {{"key", key}, {"value", {{"stringValue", value}}}};
}
}  // namespace

TelemetryExporter::TelemetryExporter(const TelemetryConfig& config)
    : config_(config)
{
  instance_trace_id_ = generate_id(16);
  if (config_.enabled && !config_.endpoint.empty())
  {
    running_ = true;
    export_thread_ = std::thread([this]() { export_loop(); });
  }
}

TelemetryExporter::~TelemetryExporter()
{
  if (running_.exchange(false))
  {
    export_cv_.notify_all();
    if (export_thread_.joinable())
    {
      export_thread_.join();
    }
  }
  // Final flush
  flush();
}

std::string TelemetryExporter::generate_id(size_t bytes) const
{
  unsigned char buf[32];
  size_t len = std::min(bytes, sizeof(buf));
  if (RAND_bytes(buf, static_cast<int>(len)) != 1)
  {
    // Fallback to random_device
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < len; ++i)
    {
      buf[i] = static_cast<unsigned char>(dist(gen));
    }
  }
  return to_hex(buf, len);
}

bool TelemetryExporter::should_sample() const
{
  if (!config_.enabled)
  {
    return false;
  }
  if (config_.sampling_rate >= 1.0)
  {
    return true;
  }
  if (config_.sampling_rate <= 0.0)
  {
    return false;
  }
  // Deterministic sampling based on random
  thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(gen) < config_.sampling_rate;
}

std::string TelemetryExporter::start_span(
    const std::string& name, const std::string& parent_span_id,
    const std::unordered_map<std::string, std::string>& attributes)
{
  Span span;
  span.trace_id = instance_trace_id_;
  span.span_id = generate_id(8);
  span.parent_id = parent_span_id;
  span.name = name;
  span.start = std::chrono::steady_clock::now();
  span.attributes = attributes;

  std::lock_guard lock(mutex_);
  active_spans_[span.span_id] = span;
  return span.span_id;
}

void TelemetryExporter::end_span(const std::string& span_id, Span::Status status,
                                  const std::string& status_message)
{
  std::lock_guard lock(mutex_);
  auto it = active_spans_.find(span_id);
  if (it == active_spans_.end())
  {
    return;
  }

  it->second.end = std::chrono::steady_clock::now();
  it->second.status = status;
  it->second.status_message = status_message;

  pending_spans_.push_back(std::move(it->second));
  active_spans_.erase(it);

  // Trigger export if batch is full
  if (pending_spans_.size() >= 100)
  {
    export_cv_.notify_one();
  }
}

void TelemetryExporter::set_attribute(const std::string& span_id, const std::string& key,
                                       const std::string& value)
{
  std::lock_guard lock(mutex_);
  auto it = active_spans_.find(span_id);
  if (it != active_spans_.end())
  {
    it->second.attributes[key] = value;
  }
}

void TelemetryExporter::record_metric(const MetricPoint& point)
{
  std::lock_guard lock(mutex_);
  pending_metrics_.push_back(point);
}

void TelemetryExporter::flush()
{
  std::vector<Span> spans;
  std::vector<MetricPoint> metrics;
  {
    std::lock_guard lock(mutex_);
    spans.swap(pending_spans_);
    metrics.swap(pending_metrics_);
  }

  if (!spans.empty())
  {
    export_spans(spans);
  }
  if (!metrics.empty())
  {
    export_metrics(metrics);
  }
}

void TelemetryExporter::export_loop()
{
  while (running_.load())
  {
    {
      std::unique_lock lock(mutex_);
      export_cv_.wait_for(lock, std::chrono::seconds(5),
                          [this]() { return !running_.load() || pending_spans_.size() >= 100; });
    }

    if (!running_.load() && pending_spans_.empty() && pending_metrics_.empty())
    {
      break;
    }

    flush();
  }
}

void TelemetryExporter::export_spans(const std::vector<Span>& spans)
{
  if (config_.endpoint.empty() || spans.empty())
  {
    return;
  }

  // Build OTLP JSON
  nlohmann::json resource_attrs = nlohmann::json::array();
  resource_attrs.push_back(make_attribute("service.name", config_.service_name));
  resource_attrs.push_back(make_attribute("service.version", "0.6.2"));
  if (!config_.environment.empty())
  {
    resource_attrs.push_back(make_attribute("deployment.environment", config_.environment));
  }
  for (const auto& [k, v] : config_.resource_attributes)
  {
    resource_attrs.push_back(make_attribute(k, v));
  }

  nlohmann::json otlp_spans = nlohmann::json::array();
  for (const auto& span : spans)
  {
    nlohmann::json attrs = nlohmann::json::array();
    for (const auto& [k, v] : span.attributes)
    {
      attrs.push_back(make_attribute(k, v));
    }

    int status_code = 0;
    switch (span.status)
    {
      case Span::Status::kOk:
        status_code = 1;
        break;
      case Span::Status::kError:
        status_code = 2;
        break;
      case Span::Status::kUnset:
        status_code = 0;
        break;
    }

    nlohmann::json span_json = {
        {"traceId", span.trace_id},
        {"spanId", span.span_id},
        {"name", span.name},
        {"kind", 2},  // SPAN_KIND_SERVER
        {"startTimeUnixNano", std::to_string(steady_to_unix_nano(span.start))},
        {"endTimeUnixNano", std::to_string(steady_to_unix_nano(span.end))},
        {"attributes", attrs},
        {"status", {{"code", status_code}}}};

    if (!span.parent_id.empty())
    {
      span_json["parentSpanId"] = span.parent_id;
    }
    if (!span.status_message.empty())
    {
      span_json["status"]["message"] = span.status_message;
    }

    otlp_spans.push_back(span_json);
  }

  nlohmann::json payload = {
      {"resourceSpans",
       {{{"resource", {{"attributes", resource_attrs}}},
         {"scopeSpans",
          {{{"scope", {{"name", "neam"}, {"version", "0.6.2"}}}, {"spans", otlp_spans}}}}}}}};

  // POST to OTLP endpoint
  std::string url = config_.endpoint;
  if (url.back() != '/')
  {
    url += "/";
  }
  url += "v1/traces";

  std::string body = payload.dump();

  CURL* handle = curl_easy_init();
  if (!handle)
  {
    return;
  }

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_POST, 1L);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, discard_write);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

  CURLcode res = curl_easy_perform(handle);
  if (res != CURLE_OK)
  {
    std::cerr << "[ERROR] [telemetry] Failed to export spans: " << curl_easy_strerror(res) << "\n";
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(handle);
}

void TelemetryExporter::export_metrics(const std::vector<MetricPoint>& metrics)
{
  if (config_.endpoint.empty() || metrics.empty())
  {
    return;
  }

  // Build OTLP metrics JSON
  nlohmann::json resource_attrs = nlohmann::json::array();
  resource_attrs.push_back(make_attribute("service.name", config_.service_name));
  resource_attrs.push_back(make_attribute("service.version", "0.6.2"));

  nlohmann::json metric_data = nlohmann::json::array();
  for (const auto& point : metrics)
  {
    nlohmann::json attrs = nlohmann::json::array();
    for (const auto& [k, v] : point.labels)
    {
      attrs.push_back(make_attribute(k, v));
    }

    auto ts_nano = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       point.timestamp.time_since_epoch())
                       .count();

    nlohmann::json dp = {{"asDouble", point.value},
                         {"timeUnixNano", std::to_string(ts_nano)},
                         {"attributes", attrs}};

    nlohmann::json metric = {
        {"name", point.name},
        {"unit", point.unit},
        {"sum", {{"dataPoints", nlohmann::json::array({dp})}, {"isMonotonic", true}}}};

    metric_data.push_back(metric);
  }

  nlohmann::json payload = {
      {"resourceMetrics",
       {{{"resource", {{"attributes", resource_attrs}}},
         {"scopeMetrics",
          {{{"scope", {{"name", "neam"}, {"version", "0.6.2"}}}, {"metrics", metric_data}}}}}}}};

  std::string url = config_.endpoint;
  if (url.back() != '/')
  {
    url += "/";
  }
  url += "v1/metrics";

  std::string body = payload.dump();

  CURL* handle = curl_easy_init();
  if (!handle)
  {
    return;
  }

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_POST, 1L);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, discard_write);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

  CURLcode res = curl_easy_perform(handle);
  if (res != CURLE_OK)
  {
    std::cerr << "[ERROR] [telemetry] Failed to export metrics: " << curl_easy_strerror(res) << "\n";
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(handle);
}

}  // namespace neamc::vm
