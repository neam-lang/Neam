//
// Neam v0.8 Phase 6 — HTTP channel adapter (pull-based for neam-api integration)
//

#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "neamc/vm/channel_adapter.hpp"

namespace neamc::vm
{

class HTTPChannelAdapter final : public ChannelAdapter
{
public:
  HTTPChannelAdapter() = default;

  void start(OnMessageCallback on_message) override;
  void send(const std::string& recipient, const OutboundMessage& msg) override;
  void stop() override;
  std::string type() const override { return "http"; }
  bool is_running() const override { return running_.load(); }

  // Pull-based API for neam-api integration
  void inject_message(const InboundMessage& msg);
  OutboundMessage last_response() const;

private:
  std::atomic<bool> running_{false};
  OnMessageCallback callback_;
  mutable std::mutex response_mutex_;
  OutboundMessage last_response_;
};

}  // namespace neamc::vm
