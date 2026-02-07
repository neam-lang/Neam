//
// Neam v0.6.5 - HTTP Client Unit Tests
//
// Tests HTTP client functionality: raw requests, retry logic,
// error handling, and response parsing.
//

#include "neamc/llm/http_client.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

namespace
{
using neamc::llm::HttpResult;

// ============================================================================
// http_post_json_raw Tests
// ============================================================================

TEST(HttpClientTest, RawPostToInvalidHostThrows)
{
  EXPECT_THROW(
      neamc::llm::http_post_json_raw("http://localhost:1/test", "{}", {}, 2000),
      std::runtime_error);
}

TEST(HttpClientTest, RawPostToInvalidUrlThrows)
{
  EXPECT_THROW(
      neamc::llm::http_post_json_raw("not-a-url", "{}", {}, 2000),
      std::runtime_error);
}

TEST(HttpClientTest, RawPostEmptyUrlThrows)
{
  EXPECT_THROW(
      neamc::llm::http_post_json_raw("", "{}", {}, 2000),
      std::runtime_error);
}

TEST(HttpClientTest, RawPostWithCustomHeadersDoesNotThrowOnFormat)
{
  // This tests that custom headers don't cause a format error
  // (will still fail with connection error, but that's the HTTP layer)
  std::vector<std::string> headers = {
      "Content-Type: application/json",
      "X-Custom-Header: test-value"};
  EXPECT_THROW(
      neamc::llm::http_post_json_raw("http://localhost:1/test", "{}", headers, 2000),
      std::runtime_error);
}

// ============================================================================
// http_post_json (with retry) Tests
// ============================================================================

TEST(HttpClientRetryTest, RetriedPostToInvalidHostThrows)
{
  // Should throw after retries are exhausted
  EXPECT_THROW(
      neamc::llm::http_post_json("http://localhost:1/test", "{}",
                                  {"Content-Type: application/json"}, 2000),
      std::runtime_error);
}

TEST(HttpClientRetryTest, PostWithEmptyBodyDoesNotCrash)
{
  // Empty body should still result in a valid HTTP request structure
  // (will fail on connection, not on request construction)
  EXPECT_THROW(
      neamc::llm::http_post_json("http://localhost:1/test", "",
                                  {"Content-Type: application/json"}, 2000),
      std::runtime_error);
}

TEST(HttpClientRetryTest, PostWithLargeBodyDoesNotCrash)
{
  // Large body should not cause buffer issues in request construction
  std::string large_body(100000, 'x');
  EXPECT_THROW(
      neamc::llm::http_post_json("http://localhost:1/test", large_body,
                                  {"Content-Type: application/json"}, 2000),
      std::runtime_error);
}

TEST(HttpClientRetryTest, PostWithNoHeadersDoesNotCrash)
{
  EXPECT_THROW(
      neamc::llm::http_post_json("http://localhost:1/test", "{}", {}, 2000),
      std::runtime_error);
}

// ============================================================================
// HttpResult Struct Tests
// ============================================================================

TEST(HttpResultTest, DefaultConstruction)
{
  HttpResult result;
  EXPECT_EQ(result.status, 0);
  EXPECT_TRUE(result.body.empty());
}

TEST(HttpResultTest, Assignment)
{
  HttpResult result;
  result.status = 200;
  result.body = "OK";
  EXPECT_EQ(result.status, 200);
  EXPECT_EQ(result.body, "OK");
}

// ============================================================================
// Timeout Tests
// ============================================================================

TEST(HttpClientTimeoutTest, ShortTimeoutStillThrows)
{
  // 1ms timeout — should fail quickly
  EXPECT_THROW(
      neamc::llm::http_post_json_raw("http://localhost:1/test", "{}", {}, 1),
      std::runtime_error);
}

TEST(HttpClientTimeoutTest, ZeroTimeoutDoesNotHang)
{
  // Zero timeout should not cause indefinite blocking
  EXPECT_THROW(
      neamc::llm::http_post_json_raw("http://localhost:1/test", "{}", {}, 0),
      std::runtime_error);
}

}  // namespace
