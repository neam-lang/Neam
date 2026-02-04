// MCP client, JSON-RPC, transport, registry tests.
#include <gtest/gtest.h>
#include "neamc/mcp/jsonrpc.hpp"
#include "neamc/mcp/registry.hpp"
#include "neamc/mcp/client.hpp"
#include <nlohmann/json.hpp>

using namespace neamc::mcp;
using json = nlohmann::json;

// ===========================================================================
// JSON-RPC encoding / decoding
// ===========================================================================

TEST(JsonRpc, EncodeRequest) {
    JsonRpcRequest req;
    req.id = 1;
    req.method = "initialize";
    req.params = json{{"key", "value"}};

    std::string encoded = encode_request(req);
    ASSERT_FALSE(encoded.empty());

    auto parsed = json::parse(encoded);
    EXPECT_EQ(parsed["jsonrpc"], "2.0");
    EXPECT_EQ(parsed["id"], 1);
    EXPECT_EQ(parsed["method"], "initialize");
    EXPECT_EQ(parsed["params"]["key"], "value");
}

TEST(JsonRpc, EncodeNotification) {
    json params = {{"progress", 50}};
    std::string encoded = encode_notification("progress", params);
    ASSERT_FALSE(encoded.empty());

    auto parsed = json::parse(encoded);
    EXPECT_EQ(parsed["method"], "progress");
    EXPECT_FALSE(parsed.contains("id"));
}

TEST(JsonRpc, DecodeSuccessResponse) {
    json j = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"result", {{"tools", json::array()}}}
    };

    auto resp = decode_response(j.dump());
    EXPECT_EQ(resp.id, 1);
    EXPECT_FALSE(resp.is_error());
    EXPECT_TRUE(resp.result.contains("tools"));
}

TEST(JsonRpc, DecodeErrorResponse) {
    json j = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"error", {{"code", -32600}, {"message", "Invalid Request"}}}
    };

    auto resp = decode_response(j.dump());
    EXPECT_EQ(resp.id, 2);
    EXPECT_TRUE(resp.is_error());
    EXPECT_EQ(resp.error_code, -32600);
    EXPECT_EQ(resp.error_message, "Invalid Request");
}

TEST(JsonRpc, DecodeInvalidJson) {
    // decode_response throws on invalid JSON
    EXPECT_THROW(decode_response("not json at all"), nlohmann::json::parse_error);
}

TEST(JsonRpc, RequestIdIncrement) {
    JsonRpcRequest req1;
    req1.id = 1;
    req1.method = "test";

    JsonRpcRequest req2;
    req2.id = 2;
    req2.method = "test";

    auto enc1 = json::parse(encode_request(req1));
    auto enc2 = json::parse(encode_request(req2));
    EXPECT_NE(enc1["id"], enc2["id"]);
}

// ===========================================================================
// McpServerConfig
// ===========================================================================

TEST(McpConfig, StdioConfig) {
    McpServerConfig config;
    config.name = "test-server";
    config.command = "python";
    config.args = {"-m", "mcp_server"};
    config.transport_type = TransportType::Stdio;

    EXPECT_EQ(config.name, "test-server");
    EXPECT_EQ(config.transport_type, TransportType::Stdio);
}

TEST(McpConfig, SSEConfig) {
    McpServerConfig config;
    config.name = "sse-server";
    config.url = "http://localhost:8080/sse";
    config.transport_type = TransportType::SSE;

    EXPECT_EQ(config.transport_type, TransportType::SSE);
}

// ===========================================================================
// McpRegistry
// ===========================================================================

TEST(McpRegistry, AddAndHasServer) {
    McpRegistry registry;
    McpServerConfig config;
    config.name = "test-server";
    config.command = "echo";
    config.transport_type = TransportType::Stdio;

    registry.add_server(config);
    EXPECT_TRUE(registry.has_server("test-server"));
    EXPECT_FALSE(registry.has_server("nonexistent"));
}

TEST(McpRegistry, UnknownServerTool) {
    McpRegistry registry;
    EXPECT_THROW(registry.call_tool("nonexistent", "tool", json{}), std::runtime_error);
}

// ===========================================================================
// McpToolDef structure
// ===========================================================================

TEST(McpToolDef, Structure) {
    McpToolDef tool;
    tool.name = "read_file";
    tool.description = "Read file contents";
    tool.input_schema = json{
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}}
    };

    EXPECT_EQ(tool.name, "read_file");
    EXPECT_TRUE(tool.input_schema.contains("properties"));
}

// ===========================================================================
// JSON-RPC edge cases
// ===========================================================================

TEST(JsonRpcEdge, EmptyMethod) {
    JsonRpcRequest req;
    req.id = 1;
    req.method = "";
    req.params = json::object();

    std::string encoded = encode_request(req);
    auto parsed = json::parse(encoded);
    EXPECT_EQ(parsed["method"], "");
}

TEST(JsonRpcEdge, NullParams) {
    JsonRpcRequest req;
    req.id = 1;
    req.method = "test";
    // Default-constructed json params

    std::string encoded = encode_request(req);
    ASSERT_FALSE(encoded.empty());
}

TEST(JsonRpcEdge, LargePayload) {
    JsonRpcRequest req;
    req.id = 1;
    req.method = "test";
    // 1MB string value
    req.params = json{{"data", std::string(1024 * 1024, 'A')}};

    std::string encoded = encode_request(req);
    EXPECT_GT(encoded.size(), 1024u * 1024u);
}

TEST(JsonRpcEdge, NotificationNoParams) {
    std::string encoded = encode_notification("ping", json{});
    auto parsed = json::parse(encoded);
    EXPECT_EQ(parsed["method"], "ping");
}
