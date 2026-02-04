#pragma once
#include "neamc/vm/state_backend.hpp"
#include "neamc/vm/autonomous.hpp"
#include <map>
#include <mutex>

namespace neamc::test {

class MockStateBackend : public vm::StateBackend {
public:
    // MemoryBackend interface
    void store_event(const std::string& store_name, const vm::MemoryEventRecord& event) override {
        std::lock_guard lock(mu_);
        stores_[store_name].push_back(event);
    }
    std::vector<vm::MemoryEventRecord> load_events(const std::string& store_name) override {
        std::lock_guard lock(mu_);
        return stores_[store_name];
    }
    void save_checkpoint(const std::string& store_name, const std::string& label,
                         std::size_t position) override {
        std::lock_guard lock(mu_);
        checkpoints_[store_name + ":" + label] = position;
    }
    std::optional<std::size_t> load_checkpoint(const std::string& store_name,
                                                const std::string& label) override {
        std::lock_guard lock(mu_);
        auto it = checkpoints_.find(store_name + ":" + label);
        if (it != checkpoints_.end()) return it->second;
        return std::nullopt;
    }
    std::vector<vm::MemoryEventRecord> search_events(const std::string& store_name,
                                                      const std::string& /*query*/,
                                                      std::size_t limit) override {
        std::lock_guard lock(mu_);
        auto& events = stores_[store_name];
        std::vector<vm::MemoryEventRecord> result;
        for (size_t i = 0; i < std::min(limit, events.size()); ++i)
            result.push_back(events[i]);
        return result;
    }
    void clear(const std::string& store_name) override {
        std::lock_guard lock(mu_);
        stores_.erase(store_name);
    }

    // StateBackend learning interface
    void store_learning_interaction(const vm::LearningInteraction& inter) override {
        interactions_.push_back(inter);
    }
    std::vector<vm::LearningInteraction> load_recent_interactions(
        const std::string& /*agent*/, size_t limit) override {
        std::vector<vm::LearningInteraction> result;
        for (size_t i = 0; i < std::min(limit, interactions_.size()); ++i)
            result.push_back(interactions_[i]);
        return result;
    }
    void store_learning_review(const vm::LearningReview& /*review*/) override {}
    void store_prompt_version(const vm::PromptVersion& ver) override {
        prompt_versions_.push_back(ver);
    }
    std::vector<vm::PromptVersion> load_prompt_history(const std::string& /*agent*/) override {
        return prompt_versions_;
    }
    bool try_update_prompt(const std::string& /*agent*/,
                           int expected_version,
                           const vm::PromptVersion& new_version) override {
        if (!prompt_versions_.empty() && prompt_versions_.back().version == expected_version) {
            prompt_versions_.push_back(new_version);
            return true;
        }
        return false;
    }
    void store_autonomous_action(const vm::AutonomousAction& /*action*/) override {}
    vm::DailyBudget load_budget(const std::string& /*agent*/,
                                 const std::string& /*date*/) override {
        return budget_;
    }
    void update_budget(const std::string& /*agent*/,
                       const vm::DailyBudget& b) override {
        budget_ = b;
    }

    // Distributed locking
    bool try_acquire_lock(const std::string& lock_name,
                          const std::string& holder_id,
                          std::chrono::seconds /*ttl*/) override {
        std::lock_guard lock(mu_);
        auto it = locks_.find(lock_name);
        if (it == locks_.end() || it->second.empty()) {
            locks_[lock_name] = holder_id;
            return true;
        }
        return it->second == holder_id;
    }
    void release_lock(const std::string& lock_name,
                      const std::string& holder_id) override {
        std::lock_guard lock(mu_);
        auto it = locks_.find(lock_name);
        if (it != locks_.end() && it->second == holder_id) {
            locks_.erase(it);
        }
    }
    bool renew_lock(const std::string& lock_name,
                    const std::string& holder_id,
                    std::chrono::seconds /*ttl*/) override {
        std::lock_guard lock(mu_);
        auto it = locks_.find(lock_name);
        return it != locks_.end() && it->second == holder_id;
    }

    void set_budget(const vm::DailyBudget& b) { budget_ = b; }

    // Check if lock is held
    bool is_lock_held(const std::string& lock_name) {
        std::lock_guard lock(mu_);
        return locks_.count(lock_name) > 0;
    }

private:
    std::map<std::string, std::vector<vm::MemoryEventRecord>> stores_;
    std::map<std::string, std::size_t> checkpoints_;
    std::vector<vm::LearningInteraction> interactions_;
    std::vector<vm::PromptVersion> prompt_versions_;
    std::map<std::string, std::string> locks_;
    vm::DailyBudget budget_;
    std::mutex mu_;
};

} // namespace neamc::test
