//
// Neam v0.8 Phase 6 — CLI channel adapter
//

#pragma once

#include <atomic>
#include <iostream>
#include <thread>

#include "neamc/vm/channel_adapter.hpp"

namespace neamc::vm
{

class CLIChannelAdapter final : public ChannelAdapter
{
public:
  CLIChannelAdapter();
  CLIChannelAdapter(std::istream& input, std::ostream& output);

  void start(OnMessageCallback on_message) override;
  void send(const std::string& recipient, const OutboundMessage& msg) override;
  void stop() override;
  std::string type() const override { return "cli"; }
  bool is_running() const override { return running_.load(); }

private:
  std::istream& input_;
  std::ostream& output_;
  std::atomic<bool> running_{false};
  OnMessageCallback callback_;
  std::thread reader_thread_;
};

}  // namespace neamc::vm
