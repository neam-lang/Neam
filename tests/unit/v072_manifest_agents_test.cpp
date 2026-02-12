//
// v0.7.2 Manifest Agents Tests — [[agents]] TOML parsing
//

#include <gtest/gtest.h>

#include "neamc/project_manifest.hpp"

TEST(ManifestAgentsTest, ParseSingleAgent)
{
  const std::string toml = R"(
[package]
name = "test-project"
version = "1.0.0"

[[agents]]
id = "support-bot"
name = "Support Bot"
provider = "openai"
model = "gpt-4o"
system-prompt = "You are a helpful support agent."
)";

  auto manifest = neamc::parse_project_manifest(toml);

  ASSERT_EQ(manifest.agents.size(), 1u);
  EXPECT_EQ(manifest.agents[0].id, "support-bot");
  EXPECT_EQ(manifest.agents[0].name, "Support Bot");
  EXPECT_EQ(manifest.agents[0].provider, "openai");
  EXPECT_EQ(manifest.agents[0].model, "gpt-4o");
  EXPECT_EQ(manifest.agents[0].system_prompt, "You are a helpful support agent.");
  EXPECT_TRUE(manifest.agents[0].source.empty());
}

TEST(ManifestAgentsTest, ParseMultipleAgents)
{
  const std::string toml = R"(
[package]
name = "multi-agent"
version = "2.0.0"

[[agents]]
id = "writer"
name = "Writer"
provider = "openai"
model = "gpt-4o"
system-prompt = "You write."

[[agents]]
id = "coder"
name = "Coder"
source = "src/coder_agent.neam"

[[agents]]
id = "analyst"
provider = "ollama"
model = "llama3"
)";

  auto manifest = neamc::parse_project_manifest(toml);

  ASSERT_EQ(manifest.agents.size(), 3u);

  EXPECT_EQ(manifest.agents[0].id, "writer");
  EXPECT_EQ(manifest.agents[0].provider, "openai");

  EXPECT_EQ(manifest.agents[1].id, "coder");
  EXPECT_EQ(manifest.agents[1].source, "src/coder_agent.neam");

  EXPECT_EQ(manifest.agents[2].id, "analyst");
  EXPECT_EQ(manifest.agents[2].provider, "ollama");
  EXPECT_EQ(manifest.agents[2].model, "llama3");
}

TEST(ManifestAgentsTest, AgentsWithKnowledgeBase)
{
  const std::string toml = R"(
[[agents]]
id = "researcher"
name = "Researcher"
provider = "openai"
model = "gpt-4o"
system-prompt = "You research."
knowledge-base = "./docs/kb.md"
)";

  auto manifest = neamc::parse_project_manifest(toml);

  ASSERT_EQ(manifest.agents.size(), 1u);
  EXPECT_EQ(manifest.agents[0].knowledge_base, "./docs/kb.md");
}

TEST(ManifestAgentsTest, NoAgentsSectionReturnsEmpty)
{
  const std::string toml = R"(
[package]
name = "no-agents"
version = "1.0.0"

[agent]
provider = "openai"
model = "gpt-4o"
)";

  auto manifest = neamc::parse_project_manifest(toml);
  EXPECT_TRUE(manifest.agents.empty());
}

TEST(ManifestAgentsTest, AgentsMixedWithOtherSections)
{
  const std::string toml = R"(
[package]
name = "mixed"
version = "1.0.0"

[deploy.serverless]
provider = "aws"
memory = 2048
provisioned-concurrency = 3

[[agents]]
id = "bot"
name = "Bot"
provider = "openai"
model = "gpt-4o-mini"
system-prompt = "Hello"

[test]
timeout = 120
)";

  auto manifest = neamc::parse_project_manifest(toml);

  // Package section parsed correctly
  EXPECT_EQ(manifest.name, "mixed");

  // Agents parsed correctly
  ASSERT_EQ(manifest.agents.size(), 1u);
  EXPECT_EQ(manifest.agents[0].id, "bot");

  // Serverless config parsed correctly
  EXPECT_EQ(manifest.deploy.serverless.memory, 2048);
  EXPECT_EQ(manifest.deploy.serverless.provisioned_concurrency, 3);

  // Test config parsed correctly
  EXPECT_EQ(manifest.test.timeout, 120);
}

TEST(ManifestAgentsTest, ProvisionedConcurrencyDefault)
{
  const std::string toml = R"(
[deploy.serverless]
provider = "aws"
memory = 1024
)";

  auto manifest = neamc::parse_project_manifest(toml);
  EXPECT_EQ(manifest.deploy.serverless.provisioned_concurrency, 0);
}
