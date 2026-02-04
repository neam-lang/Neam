// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Event loop for async voice callback dispatch
//
// Queues voice events (transcript, audio, text, tool_call, error) from
// provider background threads and dispatches them on the VM thread.
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace neamc::voice
{
// Event types produced by realtime voice sessions.
enum class VoiceEventType
{
  Transcript,
  ResponseText,
  ResponseAudio,
  ToolCall,
  Error,
  SessionClosed
};

// A single voice event.
struct VoiceEvent
{
  VoiceEventType type;
  std::string session_id;

  // Payload (which field is set depends on type)
  std::string text;                  // Transcript, ResponseText, Error
  std::vector<uint8_t> audio;       // ResponseAudio
  std::string tool_call_id;         // ToolCall
  std::string tool_name;            // ToolCall
  std::string tool_args;            // ToolCall
};

// Callback signature for event handling.
using VoiceEventHandler = std::function<void(const VoiceEvent&)>;

// Thread-safe event queue that accumulates events from provider threads
// and dispatches them on the caller's thread via poll() or run().
class VoiceEventLoop
{
public:
  VoiceEventLoop();
  ~VoiceEventLoop();

  // Push an event (thread-safe, called from provider threads).
  void push(VoiceEvent event);

  // Poll for pending events without blocking. Returns number dispatched.
  int poll(const VoiceEventHandler& handler);

  // Block until at least one event is available, then dispatch all.
  // Returns number dispatched. Returns 0 if stopped.
  int poll_blocking(const VoiceEventHandler& handler);

  // Block until at least one event is available or timeout_ms elapses.
  // Returns number dispatched.
  int poll_timeout(const VoiceEventHandler& handler, int timeout_ms);

  // Run the event loop until stop() is called.
  void run(const VoiceEventHandler& handler);

  // Signal the loop to stop (thread-safe).
  void stop();

  // Check if running.
  bool is_running() const { return running_.load(); }

  // Number of events in the queue.
  std::size_t pending() const;

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<VoiceEvent> queue_;
  std::atomic<bool> running_{false};
};
}  // namespace neamc::voice
