#pragma once
#include "neamc/mcp/transport.hpp"
#include <queue>
#include <mutex>

namespace neamc::test {

/// Mock MCP transport with in-memory request/response pairs.
class MockTransport : public mcp::Transport {
public:
    void queue_response(const std::string& response) {
        std::lock_guard lock(mu_);
        responses_.push(response);
    }

    void send(const std::string& data) override {
        std::lock_guard lock(mu_);
        sent_messages_.push_back(data);
    }

    std::string receive() override {
        std::lock_guard lock(mu_);
        if (responses_.empty()) return "";
        auto r = responses_.front();
        responses_.pop();
        return r;
    }

    void close() override { open_ = false; }
    bool is_open() const override { return open_; }

    void set_open(bool open) { open_ = open; }

    const std::vector<std::string>& sent_messages() const { return sent_messages_; }

private:
    std::queue<std::string> responses_;
    std::vector<std::string> sent_messages_;
    std::mutex mu_;
    bool open_ = true;
};

} // namespace neamc::test
