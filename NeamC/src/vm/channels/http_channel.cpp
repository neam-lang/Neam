//
// Neam v0.8 Phase 6 — HTTP channel adapter implementation
//

#include "neamc/vm/channels/http_channel.hpp"

namespace neamc::vm
{

void HTTPChannelAdapter::start(OnMessageCallback on_message)
{
  callback_ = std::move(on_message);
  running_ = true;
}

void HTTPChannelAdapter::send(const std::string& /*recipient*/, const OutboundMessage& msg)
{
  std::lock_guard<std::mutex> lock(response_mutex_);
  last_response_ = msg;
}

void HTTPChannelAdapter::stop()
{
  running_ = false;
  callback_ = nullptr;
}

void HTTPChannelAdapter::inject_message(const InboundMessage& msg)
{
  if (running_.load() && callback_)
    callback_(msg);
}

OutboundMessage HTTPChannelAdapter::last_response() const
{
  std::lock_guard<std::mutex> lock(response_mutex_);
  return last_response_;
}

}  // namespace neamc::vm
