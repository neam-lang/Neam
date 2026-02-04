// OpenTelemetry spans, metrics, sampling tests.
#include <gtest/gtest.h>
#include "neamc/vm/telemetry.hpp"

using namespace neamc::vm;

// ===========================================================================
// TelemetryConfig
// ===========================================================================

TEST(TelemetryConfig, Defaults) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "neam-test";
    config.environment = "test";
    config.sampling_rate = 1.0;

    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.service_name, "neam-test");
}

TEST(TelemetryConfig, WithEndpoint) {
    TelemetryConfig config;
    config.enabled = true;
    config.endpoint = "http://localhost:4317";
    config.protocol = "grpc";
    config.service_name = "neam-app";

    EXPECT_EQ(config.endpoint, "http://localhost:4317");
}

// ===========================================================================
// TelemetryExporter - Spans
// ===========================================================================

TEST(TelemetryExporter, StartAndEndSpan) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);

    std::string span_id = exporter.start_span("test-operation", "", {});
    ASSERT_FALSE(span_id.empty());

    exporter.end_span(span_id, Span::Status::kOk, "");
}

TEST(TelemetryExporter, SetAttribute) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);

    std::string span_id = exporter.start_span("op", "", {});
    exporter.set_attribute(span_id, "key", "value");
    exporter.end_span(span_id, Span::Status::kOk, "");
}

TEST(TelemetryExporter, ChildSpan) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);

    std::string parent = exporter.start_span("parent", "", {});
    std::string child = exporter.start_span("child", parent, {});

    EXPECT_NE(parent, child);

    exporter.end_span(child, Span::Status::kOk, "");
    exporter.end_span(parent, Span::Status::kOk, "");
}

TEST(TelemetryExporter, SpanWithError) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);

    std::string span_id = exporter.start_span("failing-op", "", {});
    exporter.end_span(span_id, Span::Status::kError, "something went wrong");
}

TEST(TelemetryExporter, SpanWithAttributes) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);

    std::unordered_map<std::string, std::string> attrs = {
        {"agent", "TestBot"},
        {"model", "gpt-4"}
    };
    std::string span_id = exporter.start_span("agent-call", "", attrs);
    exporter.end_span(span_id, Span::Status::kOk, "");
}

// ===========================================================================
// Metrics
// ===========================================================================

TEST(TelemetryExporter, RecordMetric) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);

    MetricPoint metric;
    metric.name = "request_count";
    metric.unit = "count";
    metric.value = 1.0;
    metric.labels = {{"endpoint", "/api/chat"}};
    metric.timestamp = std::chrono::system_clock::now();

    exporter.record_metric(metric);
}

TEST(TelemetryExporter, Flush) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);
    exporter.start_span("test", "", {});
    exporter.flush();
}

// ===========================================================================
// Sampling
// ===========================================================================

TEST(TelemetryExporter, SamplingRate100Percent) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);
    EXPECT_TRUE(exporter.should_sample());
}

TEST(TelemetryExporter, SamplingRate0Percent) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 0.0;

    TelemetryExporter exporter(config);
    EXPECT_FALSE(exporter.should_sample());
}

// ===========================================================================
// Span structure
// ===========================================================================

TEST(SpanStructure, Fields) {
    Span span;
    span.trace_id = "trace-abc-123";
    span.span_id = "span-xyz";
    span.parent_id = "";
    span.name = "test-op";
    span.start = std::chrono::steady_clock::now();
    span.end = std::chrono::steady_clock::now();
    span.status = Span::Status::kOk;
    span.attributes["key"] = "value";

    EXPECT_EQ(span.name, "test-op");
    EXPECT_EQ(span.status, Span::Status::kOk);
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(TelemetryEdge, EndNonexistentSpan) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);
    EXPECT_NO_THROW(exporter.end_span("nonexistent-id", Span::Status::kOk, ""));
}

TEST(TelemetryEdge, AttributeOnClosedSpan) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);
    std::string span_id = exporter.start_span("op", "", {});
    exporter.end_span(span_id, Span::Status::kOk, "");

    EXPECT_NO_THROW(exporter.set_attribute(span_id, "key", "value"));
}

TEST(TelemetryEdge, ManySpans) {
    TelemetryConfig config;
    config.enabled = true;
    config.service_name = "test";
    config.sampling_rate = 1.0;

    TelemetryExporter exporter(config);

    for (int i = 0; i < 1000; ++i) {
        std::string id = exporter.start_span("span-" + std::to_string(i), "", {});
        exporter.end_span(id, Span::Status::kOk, "");
    }

    exporter.flush();
}
