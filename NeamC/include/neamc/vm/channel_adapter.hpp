//
// Neam v0.8 Phase 6 — Channel adapter abstraction
//

#pragma once

#include <functional>
#include <string>
#include <unordered_map>

namespace neamc::vm
{

struct InboundMessage
{
  std::string channel_type;
  std::string scope;        // "dm", "group", "broadcast"
  std::string sender;
  std::string content;
  std::string session_key;
  std::string metadata_json;
};

struct OutboundMessage
{
  std::string content;
  std::string metadata_json;
};

using OnMessageCallback = std::function<void(const InboundMessage&)>;

class ChannelAdapter
{
public:
  virtual ~ChannelAdapter() = default;

  virtual void start(OnMessageCallback on_message) = 0;
  virtual void send(const std::string& recipient, const OutboundMessage& msg) = 0;
  virtual void stop() = 0;
  virtual std::string type() const = 0;
  virtual bool is_running() const = 0;
};

}  // namespace neamc::vm
