// AutonomousExecutor and DistributedAutonomousExecutor tests.
#include <gtest/gtest.h>
#include "neamc/vm/autonomous.hpp"
#include "neamc/vm/distributed_autonomous.hpp"
#include "../mocks/mock_state_backend.hpp"
#include <thread>
#include <chrono>

using namespace neamc::vm;
using namespace neamc::test;

// ===========================================================================
// AutonomousAgentConfig
// ===========================================================================

TEST(AutonomousConfig, Defaults) {
    AutonomousAgentConfig config;
    config.agent_name = "TestBot";
    config.goals = {"goal1", "goal2"};
    config.schedule = "every 5m";
    config.initiative = true;
    config.max_daily_calls = 100;
    config.max_daily_cost = 10.0;
    config.max_daily_tokens = 50000;

    EXPECT_EQ(config.agent_name, "TestBot");
    EXPECT_EQ(config.goals.size(), 2u);
}

// ===========================================================================
// AutonomousExecutor
// ===========================================================================

TEST(AutonomousExecutor, CreateAndDestroy) {
    AutonomousExecutor executor;
    EXPECT_FALSE(executor.is_running());
}

TEST(AutonomousExecutor, RegisterAgent) {
    AutonomousExecutor executor;
    AutonomousAgentConfig config;
    config.agent_name = "Bot";
    config.goals = {"help users"};
    config.schedule = "every 5m";
    config.max_daily_calls = 50;
    config.max_daily_cost = 5.0;
    config.max_daily_tokens = 10000;

    executor.register_agent(config);
}

TEST(AutonomousExecutor, UnregisterAgent) {
    AutonomousExecutor executor;
    AutonomousAgentConfig config;
    config.agent_name = "Bot";
    config.goals = {"help"};
    config.schedule = "every 5m";
    config.max_daily_calls = 10;
    config.max_daily_cost = 1.0;
    config.max_daily_tokens = 1000;

    executor.register_agent(config);
    executor.unregister_agent("Bot");
}

TEST(AutonomousExecutor, PauseResume) {
    AutonomousExecutor executor;
    AutonomousAgentConfig config;
    config.agent_name = "Bot";
    config.goals = {"test"};
    config.schedule = "every 1h";
    config.max_daily_calls = 10;
    config.max_daily_cost = 1.0;
    config.max_daily_tokens = 1000;

    executor.register_agent(config);
    executor.pause_agent("Bot");
    EXPECT_TRUE(executor.is_paused("Bot"));

    executor.resume_agent("Bot");
    EXPECT_FALSE(executor.is_paused("Bot"));
}

TEST(AutonomousExecutor, SetAndGetGoals) {
    AutonomousExecutor executor;
    AutonomousAgentConfig config;
    config.agent_name = "Bot";
    config.goals = {"original goal"};
    config.schedule = "every 1h";
    config.max_daily_calls = 10;
    config.max_daily_cost = 1.0;
    config.max_daily_tokens = 1000;

    executor.register_agent(config);

    std::vector<std::string> new_goals = {"new goal 1", "new goal 2"};
    executor.set_goals("Bot", new_goals);

    auto goals = executor.get_goals("Bot");
    EXPECT_EQ(goals.size(), 2u);
    EXPECT_EQ(goals[0], "new goal 1");
}

TEST(AutonomousExecutor, StartStop) {
    AutonomousExecutor executor;

    executor.set_agent_call_fn([](const std::string&, const std::string&) {
        return "mock response";
    });
    executor.set_log_fn([](const std::string&, const std::string&, const std::string&) {});

    executor.start();
    EXPECT_TRUE(executor.is_running());

    executor.stop();
    EXPECT_FALSE(executor.is_running());
}

TEST(AutonomousExecutor, DuplicateRegister) {
    AutonomousExecutor executor;
    AutonomousAgentConfig config;
    config.agent_name = "Bot";
    config.goals = {"test"};
    config.schedule = "every 1h";
    config.max_daily_calls = 10;
    config.max_daily_cost = 1.0;
    config.max_daily_tokens = 1000;

    executor.register_agent(config);
    EXPECT_NO_THROW(executor.register_agent(config));
}

TEST(AutonomousExecutor, UnregisterNonexistent) {
    AutonomousExecutor executor;
    EXPECT_NO_THROW(executor.unregister_agent("nonexistent"));
}

// ===========================================================================
// DailyBudget
// ===========================================================================

TEST(DailyBudget, Structure) {
    DailyBudget budget;
    budget.date = "2024-01-01";
    budget.calls_used = 5;
    budget.tokens_used = 1000;
    budget.cost_used = 0.50;

    EXPECT_EQ(budget.date, "2024-01-01");
    EXPECT_EQ(budget.calls_used, 5u);
}

// ===========================================================================
// DistributedAutonomousExecutor
// ===========================================================================

TEST(DistributedAutonomous, CreateWithInstanceId) {
    DistributedAutonomousExecutor executor;
    executor.set_instance_id("instance-abc");
    EXPECT_EQ(executor.instance_id(), "instance-abc");
}

TEST(DistributedAutonomous, SetStateBackend) {
    DistributedAutonomousExecutor executor;
    auto backend = std::make_shared<MockStateBackend>();
    executor.set_state_backend(backend);
}

TEST(DistributedAutonomous, LeaderElection) {
    auto backend = std::make_shared<MockStateBackend>();

    DistributedAutonomousExecutor exec1;
    exec1.set_instance_id("instance-1");
    exec1.set_state_backend(backend);
    exec1.set_agent_call_fn([](const std::string&, const std::string&) {
        return "mock";
    });
    exec1.set_log_fn([](const std::string&, const std::string&, const std::string&) {});
}

TEST(DistributedAutonomous, BudgetPersistence) {
    auto backend = std::make_shared<MockStateBackend>();
    DailyBudget b;
    b.date = "2024-01-01";
    b.calls_used = 5;
    b.tokens_used = 1000;
    b.cost_used = 0.50;
    backend->set_budget(b);

    DistributedAutonomousExecutor executor;
    executor.set_instance_id("instance-1");
    executor.set_state_backend(backend);

    auto budget = backend->load_budget("Bot", "2024-01-01");
    EXPECT_EQ(budget.calls_used, 5u);
}

TEST(DistributedAutonomous, NonLeaderIdle) {
    auto backend = std::make_shared<MockStateBackend>();

    // First instance acquires lock
    backend->try_acquire_lock("autonomous_leader", "instance-1", std::chrono::seconds(30));

    DistributedAutonomousExecutor exec2;
    exec2.set_instance_id("instance-2");
    exec2.set_state_backend(backend);

    bool acquired = backend->try_acquire_lock("autonomous_leader", "instance-2", std::chrono::seconds(30));
    EXPECT_FALSE(acquired);
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(AutonomousEdge, ZeroBudget) {
    AutonomousAgentConfig config;
    config.agent_name = "Bot";
    config.goals = {"test"};
    config.schedule = "every 1h";
    config.max_daily_calls = 0;
    config.max_daily_cost = 0.0;
    config.max_daily_tokens = 0;

    AutonomousExecutor executor;
    executor.register_agent(config);
}

TEST(AutonomousEdge, EmptyGoals) {
    AutonomousAgentConfig config;
    config.agent_name = "Bot";
    config.goals = {};
    config.schedule = "every 1h";
    config.max_daily_calls = 10;
    config.max_daily_cost = 1.0;
    config.max_daily_tokens = 1000;

    AutonomousExecutor executor;
    EXPECT_NO_THROW(executor.register_agent(config));
}

TEST(AutonomousEdge, PauseNonexistent) {
    AutonomousExecutor executor;
    EXPECT_NO_THROW(executor.pause_agent("nonexistent"));
}
