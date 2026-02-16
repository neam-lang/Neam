//
// Neam v0.8 Phase 6 — CLI channel adapter implementation
//

#include "neamc/vm/channels/cli_channel.hpp"

namespace neamc::vm
{

CLIChannelAdapter::CLIChannelAdapter()
    : input_(std::cin), output_(std::cout)
{
}

CLIChannelAdapter::CLIChannelAdapter(std::istream& input, std::ostream& output)
    : input_(input), output_(output)
{
}

void CLIChannelAdapter::start(OnMessageCallback on_message)
{
  if (running_.load()) return;
  callback_ = std::move(on_message);
  running_ = true;

  reader_thread_ = std::thread([this]()
  {
    std::string line;
    while (running_.load() && std::getline(input_, line))
    {
      if (!running_.load()) break;
      InboundMessage msg;
      msg.channel_type = "cli";
      msg.scope = "dm";
      msg.sender = "cli_user";
      msg.content = line;
      msg.session_key = "default";
      if (callback_) callback_(msg);
    }
    running_ = false;
  });
}

void CLIChannelAdapter::send(const std::string& /*recipient*/, const OutboundMessage& msg)
{
  output_ << msg.content << std::endl;
}

void CLIChannelAdapter::stop()
{
  running_ = false;
  if (reader_thread_.joinable())
    reader_thread_.join();
}

}  // namespace neamc::vm
