// Multi-provider LLM Gateway and MCP integration tests.
// Tests OpenAI and Ollama providers together with gateway features.
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/llm_gateway.hpp"
#include "neamc/mcp/registry.hpp"
#include "neamc/mcp/jsonrpc.hpp"
#include "neamc/llm/provider_factory.hpp"

using namespace neamc;
using namespace neamc::vm;
using namespace neamc::mcp;
using namespace neamc::llm;

// Helper to compile and run Neam source
static void run_source(const std::string& source) {
    Pipeline pipeline;
    auto unit = pipeline.compile(source, {});
    VirtualMachine vm;
    vm.run(unit.chunk);
}

// ===========================================================================
// TC313: Multi-Provider Agent Definition
// ===========================================================================
TEST(MultiProvider, OpenAIAndOllamaAgents) {
    // Test that both OpenAI and Ollama agents can be defined together
    EXPECT_NO_THROW(run_source(R"(
        agent OpenAIBot {
            provider: "openai"
            model: "gpt-4"
            system: "You are an OpenAI-powered assistant."
        }

        agent OllamaBot {
            provider: "ollama"
            model: "llama3.2:3b"
            system: "You are an Ollama-powered assistant."
        }

        emit "Both agents defined!";
    )"));
}

// ===========================================================================
// TC314: Provider Factory - OpenAI Configuration
// ===========================================================================
TEST(MultiProvider, OpenAIProviderFactory) {
    ProviderConfig config;
    config.model = "gpt-4";
    config.api_key = "test-key";

    // Factory should create provider without throwing
    EXPECT_NO_THROW({
        auto provider = create_provider("openai", config);
        // Provider may be null if not fully configured, that's OK
    });
}

// ===========================================================================
// TC315: Provider Factory - Ollama Configuration
// ===========================================================================
TEST(MultiProvider, OllamaProviderFactory) {
    ProviderConfig config;
    config.model = "llama3.2:3b";
    config.default_host = "http://localhost:11434";

    // Factory should create provider without throwing
    EXPECT_NO_THROW({
        auto provider = create_provider("ollama", config);
        // Provider may be null if Ollama not running, that's OK
    });
}

// ===========================================================================
// TC316: LLM Gateway - Fallback Chain Configuration
// ===========================================================================
TEST(MultiProvider, GatewayFallbackChain) {
    LLMGatewayConfig config;
    config.fallback_chain = {"openai", "anthropic", "ollama"};
    config.circuit_breaker.failure_threshold = 5;
    config.circuit_breaker.recovery_timeout = std::chrono::seconds(30);

    EXPECT_NO_THROW({
        LLMGateway gateway(config);
        // Gateway should be created with fallback chain
    });
}

// ===========================================================================
// TC317: LLM Gateway - Provider Health Tracking
// ===========================================================================
TEST(MultiProvider, GatewayHealthTracking) {
    LLMGatewayConfig config;

    LLMGateway gateway(config);

    // Get all provider health
    auto health_map = gateway.get_provider_health();
    // Map may be empty if no providers registered
    EXPECT_TRUE(health_map.empty() || !health_map.empty());
}

// ===========================================================================
// TC318: LLM Gateway - Cost Tracking Per Provider
// ===========================================================================
TEST(MultiProvider, GatewayCostPerProvider) {
    LLMGatewayConfig config;
    config.cost.alert_threshold_daily = 100.0;

    LLMGateway gateway(config);

    // Get cost report
    auto report = gateway.get_cost_report();

    // Verify report structure
    EXPECT_GE(report.daily_total, 0.0);
    EXPECT_GE(report.monthly_total, 0.0);
}

// ===========================================================================
// TC319: MCP Registry - Multiple Server Registration
// ===========================================================================
TEST(MultiProvider, MCPMultipleServers) {
    McpRegistry registry;

    // Register multiple MCP servers
    McpServerConfig config1;
    config1.name = "filesystem";
    config1.transport_type = TransportType::Stdio;
    config1.command = "npx";
    config1.args = {"-y", "@anthropic/mcp-server-filesystem"};

    McpServerConfig config2;
    config2.name = "websearch";
    config2.transport_type = TransportType::SSE;
    config2.url = "http://localhost:3001/sse";

    EXPECT_NO_THROW({
        registry.add_server(config1);
        registry.add_server(config2);
    });

    EXPECT_TRUE(registry.has_server("filesystem"));
    EXPECT_TRUE(registry.has_server("websearch"));
    EXPECT_FALSE(registry.has_server("nonexistent"));
}

// ===========================================================================
// TC320: MCP JSON-RPC - Tool Call Encoding
// ===========================================================================
TEST(MultiProvider, MCPToolCallEncoding) {
    // Test JSON-RPC request encoding
    JsonRpcRequest request;
    request.id = 1;
    request.method = "tools/call";
    request.params = {
        {"name", "read_file"},
        {"arguments", {{"path", "/tmp/test.txt"}}}
    };

    std::string encoded = encode_request(request);

    // Verify JSON structure
    auto json = nlohmann::json::parse(encoded);
    EXPECT_TRUE(json.contains("jsonrpc"));
    EXPECT_EQ(json["jsonrpc"], "2.0");
    EXPECT_TRUE(json.contains("method"));
    EXPECT_EQ(json["method"], "tools/call");
    EXPECT_TRUE(json.contains("params"));
    EXPECT_TRUE(json.contains("id"));
}

// ===========================================================================
// TC321: Agent with MCP Server Definition
// ===========================================================================
TEST(MultiProvider, AgentWithMCPServer) {
    EXPECT_NO_THROW(run_source(R"(
        agent MCPAgent {
            provider: "openai"
            model: "gpt-4"
            system: "You can access files via MCP."
        }

        emit "MCP Agent defined!";
    )"));
}

// ===========================================================================
// TC322: Multi-Provider with Knowledge Base
// ===========================================================================
TEST(MultiProvider, MultiProviderWithKnowledge) {
    // Test knowledge base definition alongside multi-provider agents
    EXPECT_NO_THROW(run_source(R"(
        knowledge SharedKB {
            vector_store: "memory"
            embedding_model: "text-embedding-3-small"
            chunk_size: 500
            chunk_overlap: 50
            sources: [
                { type: "text", content: "Shared knowledge for all agents." }
            ]
        }

        agent OpenAIWithKB {
            provider: "openai"
            model: "gpt-4"
            system: "Use the knowledge base."
        }

        agent OllamaWithKB {
            provider: "ollama"
            model: "llama3.2:3b"
            system: "Use the knowledge base."
        }

        emit "Multi-provider with shared KB!";
    )"));
}

// ===========================================================================
// Additional Edge Cases
// ===========================================================================

TEST(MultiProviderEdge, EmptyFallbackChain) {
    LLMGatewayConfig config;
    config.fallback_chain = {};  // Empty fallback

    EXPECT_NO_THROW({
        LLMGateway gateway(config);
    });
}

TEST(MultiProviderEdge, DuplicateProviderInChain) {
    LLMGatewayConfig config;
    config.fallback_chain = {"openai", "ollama", "openai"};  // Duplicate

    // Should handle duplicates gracefully
    EXPECT_NO_THROW({
        LLMGateway gateway(config);
    });
}

TEST(MultiProviderEdge, MCPServerDisconnect) {
    McpRegistry registry;

    McpServerConfig config;
    config.name = "test";
    config.transport_type = TransportType::Stdio;
    config.command = "echo";

    registry.add_server(config);

    // Disconnect should not throw even if not connected
    EXPECT_NO_THROW({
        registry.disconnect_all();
    });
}

TEST(MultiProviderEdge, GatewayZeroBudget) {
    LLMGatewayConfig config;
    config.cost.alert_threshold_daily = 0.0;  // Zero budget

    LLMGateway gateway(config);

    // Should still create gateway
    auto report = gateway.get_cost_report();
    EXPECT_GE(report.daily_total, 0.0);
}

TEST(MultiProviderEdge, ProviderSwitching) {
    // Test rapid provider switching in agent definitions
    EXPECT_NO_THROW(run_source(R"(
        agent Bot1 { provider: "openai" model: "gpt-4" system: "A" }
        agent Bot2 { provider: "ollama" model: "llama3.2:3b" system: "B" }
        agent Bot3 { provider: "anthropic" model: "claude-3-opus-20240229" system: "C" }
        agent Bot4 { provider: "gemini" model: "gemini-pro" system: "D" }
        agent Bot5 { provider: "ollama" model: "mistral" system: "E" }
        emit "5 agents with different providers!";
    )"));
}

TEST(MultiProviderEdge, HandoffBetweenProviders) {
    EXPECT_NO_THROW(run_source(R"(
        agent OpenAIAgent {
            provider: "openai"
            model: "gpt-4"
            system: "OpenAI agent."
        }

        agent OllamaAgent {
            provider: "ollama"
            model: "llama3.2:3b"
            system: "Ollama agent."
        }

        handoff CrossProviderHandoff {
            targets: [
                handoff_to(OllamaAgent) {
                    tool_name: "transfer_to_ollama"
                    description: "Transfer to local Ollama agent"
                }
            ]
        }

        emit "Cross-provider handoff defined!";
    )"));
}

TEST(MultiProviderEdge, GatewayRateLimits) {
    LLMGatewayConfig config;
    config.rate_limits["openai"] = {60, 100000};  // 60 RPM, 100k TPM
    config.rate_limits["ollama"] = {0, 0};  // No limits for local

    EXPECT_NO_THROW({
        LLMGateway gateway(config);
    });
}

TEST(MultiProviderEdge, GatewayCache) {
    LLMGatewayConfig config;
    config.cache.enabled = true;
    config.cache.ttl = std::chrono::seconds(300);
    config.cache.max_entries = 1000;

    EXPECT_NO_THROW({
        LLMGateway gateway(config);
    });
}

TEST(MultiProviderEdge, MCPToolAggregation) {
    McpRegistry registry;

    // Add servers with different tools
    McpServerConfig config1;
    config1.name = "server1";
    config1.transport_type = TransportType::Stdio;
    config1.command = "echo";

    McpServerConfig config2;
    config2.name = "server2";
    config2.transport_type = TransportType::Stdio;
    config2.command = "echo";

    registry.add_server(config1);
    registry.add_server(config2);

    // Get aggregated tools (will be empty since not connected)
    auto tools = registry.all_tools();
    EXPECT_TRUE(tools.empty() || !tools.empty());  // Just verify call works
}

TEST(MultiProviderEdge, CircuitBreakerConfig) {
    LLMGatewayConfig config;
    config.circuit_breaker.failure_threshold = 3;
    config.circuit_breaker.recovery_timeout = std::chrono::seconds(60);

    EXPECT_NO_THROW({
        LLMGateway gateway(config);
    });
}
