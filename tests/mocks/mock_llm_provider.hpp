#pragma once
#include "neamc/llm/provider.hpp"
#include "neamc/vm/async/future.hpp"
#include <queue>
#include <mutex>

namespace neamc::test {

/// Mock LLM provider that returns canned responses.
class MockLLMProvider : public llm::LLMProvider {
public:
    explicit MockLLMProvider(std::string default_response = "mock response")
        : default_response_(std::move(default_response)) {}

    void queue_response(const std::string& response) {
        std::lock_guard lock(mu_);
        responses_.push(response);
    }

    void set_should_fail(bool fail) { should_fail_ = fail; }
    int call_count() const { return call_count_; }

    vm::async::Future<std::string> complete_async(const std::string& prompt) override {
        return vm::async::make_ready_future(complete(prompt));
    }

    std::string complete(const std::string& /*prompt*/) override {
        ++call_count_;
        if (should_fail_) throw std::runtime_error("mock provider failure");
        std::lock_guard lock(mu_);
        if (!responses_.empty()) {
            auto r = responses_.front();
            responses_.pop();
            return r;
        }
        return default_response_;
    }

    std::string chat(const std::vector<llm::Message>& /*messages*/) override {
        return complete("");
    }

    llm::ChatResult chat_with_tools(
        const std::vector<llm::Message>& /*messages*/,
        const std::vector<llm::ToolDefinition>& /*tools*/,
        const std::string& /*response_format*/) override {
        ++call_count_;
        if (should_fail_) throw std::runtime_error("mock provider failure");
        llm::ChatResult result;
        std::lock_guard lock(mu_);
        if (!responses_.empty()) {
            result.content = responses_.front();
            responses_.pop();
        } else {
            result.content = default_response_;
        }
        result.usage.prompt_tokens = 10;
        result.usage.completion_tokens = 20;
        result.usage.total_tokens = 30;
        return result;
    }

private:
    std::string default_response_;
    std::queue<std::string> responses_;
    std::mutex mu_;
    std::atomic<int> call_count_{0};
    std::atomic<bool> should_fail_{false};
};

} // namespace neamc::test
