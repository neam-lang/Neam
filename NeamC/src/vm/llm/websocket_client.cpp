// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - WebSocket client implementation
//
// Minimal WebSocket client using POSIX sockets + OpenSSL for TLS.
// Implements RFC 6455 framing with masking (client-side).
//

#include "neamc/llm/websocket_client.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
#endif

#include "neamc/util/base64.hpp"

namespace neamc::llm
{
namespace
{
#ifdef _WIN32
struct WinsockInit
{
  WinsockInit()
  {
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
  }
  ~WinsockInit() { WSACleanup(); }
};

const WinsockInit& ensure_winsock()
{
  static const WinsockInit init;
  return init;
}
#endif

void close_socket(socket_t sock)
{
  if (sock == INVALID_SOCK)
  {
    return;
  }
#ifdef _WIN32
  closesocket(sock);
#else
  ::close(sock);
#endif
}

std::string generate_ws_key()
{
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint8_t> dist(0, 255);
  std::vector<uint8_t> key(16);
  for (auto& b : key)
  {
    b = dist(gen);
  }
  return util::base64_encode(key);
}

// Parse URL into components
struct UrlParts
{
  bool tls = false;
  std::string host;
  uint16_t port = 0;
  std::string path;
};

UrlParts parse_ws_url(const std::string& url)
{
  UrlParts parts;
  std::string rest;
  if (url.substr(0, 6) == "wss://")
  {
    parts.tls = true;
    parts.port = 443;
    rest = url.substr(6);
  }
  else if (url.substr(0, 5) == "ws://")
  {
    parts.tls = false;
    parts.port = 80;
    rest = url.substr(5);
  }
  else
  {
    throw std::runtime_error("WebSocket URL must start with ws:// or wss://");
  }

  auto path_pos = rest.find('/');
  std::string host_port;
  if (path_pos != std::string::npos)
  {
    parts.path = rest.substr(path_pos);
    host_port = rest.substr(0, path_pos);
  }
  else
  {
    parts.path = "/";
    host_port = rest;
  }

  auto colon_pos = host_port.find(':');
  if (colon_pos != std::string::npos)
  {
    parts.host = host_port.substr(0, colon_pos);
    parts.port = static_cast<uint16_t>(std::stoi(host_port.substr(colon_pos + 1)));
  }
  else
  {
    parts.host = host_port;
  }
  return parts;
}

// Read exactly n bytes from socket/SSL
bool read_exact(SSL* ssl, socket_t sock, void* buf, size_t len)
{
  size_t read_total = 0;
  auto* ptr = static_cast<uint8_t*>(buf);
  while (read_total < len)
  {
    int n;
    if (ssl)
    {
      n = SSL_read(ssl, ptr + read_total, static_cast<int>(len - read_total));
    }
    else
    {
      n = static_cast<int>(recv(sock, reinterpret_cast<char*>(ptr + read_total),
                                 len - read_total, 0));
    }
    if (n <= 0)
    {
      return false;
    }
    read_total += static_cast<size_t>(n);
  }
  return true;
}

bool send_all(SSL* ssl, socket_t sock, const void* buf, size_t len)
{
  size_t sent = 0;
  auto* ptr = static_cast<const uint8_t*>(buf);
  while (sent < len)
  {
    int n;
    if (ssl)
    {
      n = SSL_write(ssl, ptr + sent, static_cast<int>(len - sent));
    }
    else
    {
      n = static_cast<int>(::send(sock, reinterpret_cast<const char*>(ptr + sent),
                                   len - sent, 0));
    }
    if (n <= 0)
    {
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

// WebSocket opcodes
constexpr uint8_t WS_OP_CONTINUATION = 0x0;
constexpr uint8_t WS_OP_TEXT = 0x1;
constexpr uint8_t WS_OP_BINARY = 0x2;
constexpr uint8_t WS_OP_CLOSE = 0x8;
constexpr uint8_t WS_OP_PING = 0x9;
constexpr uint8_t WS_OP_PONG = 0xA;
}  // namespace

struct WebSocketClient::Impl
{
  socket_t sock = INVALID_SOCK;
  SSL_CTX* ssl_ctx = nullptr;
  SSL* ssl = nullptr;
  std::atomic<bool> connected{false};
  std::thread recv_thread;

  MessageCallback on_message;
  BinaryCallback on_binary;
  ErrorCallback on_error;
  CloseCallback on_close;
  std::mutex send_mutex;
  std::mutex callback_mutex;

  ~Impl()
  {
    close_connection(1000, "");
  }

  void connect(const std::string& url, const std::vector<std::string>& headers)
  {
#ifndef _WIN32
    // Ignore SIGPIPE globally so broken WebSocket writes don't kill the process
    signal(SIGPIPE, SIG_IGN);
#endif

    auto parts = parse_ws_url(url);

    // Resolve host
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;
    std::string port_str = std::to_string(parts.port);

    if (getaddrinfo(parts.host.c_str(), port_str.c_str(), &hints, &result) != 0)
    {
      throw std::runtime_error("Failed to resolve host: " + parts.host);
    }

    sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCK)
    {
      freeaddrinfo(result);
      throw std::runtime_error("Failed to create socket");
    }

    // Suppress SIGPIPE on this socket (macOS)
#ifdef SO_NOSIGPIPE
    int nosigpipe = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE,
               reinterpret_cast<const char*>(&nosigpipe), sizeof(nosigpipe));
#endif

    // Set socket timeout (30 seconds)
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    if (::connect(sock, result->ai_addr, static_cast<socklen_t>(result->ai_addrlen)) != 0)
    {
      freeaddrinfo(result);
      close_socket(sock);
      sock = INVALID_SOCK;
      throw std::runtime_error("Failed to connect to " + parts.host + ":" + port_str);
    }
    freeaddrinfo(result);

    // TCP_NODELAY for low latency
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag),
               sizeof(flag));

    // TLS handshake if wss://
    if (parts.tls)
    {
      ssl_ctx = SSL_CTX_new(TLS_client_method());
      if (!ssl_ctx)
      {
        close_socket(sock);
        sock = INVALID_SOCK;
        throw std::runtime_error("Failed to create SSL context");
      }
      SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);
      SSL_CTX_set_default_verify_paths(ssl_ctx);

      ssl = SSL_new(ssl_ctx);
      SSL_set_fd(ssl, static_cast<int>(sock));
      SSL_set_tlsext_host_name(ssl, parts.host.c_str());

      if (SSL_connect(ssl) != 1)
      {
        SSL_free(ssl);
        ssl = nullptr;
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
        close_socket(sock);
        sock = INVALID_SOCK;
        throw std::runtime_error("TLS handshake failed for " + parts.host);
      }
    }

    // WebSocket upgrade handshake
    std::string ws_key = generate_ws_key();
    std::ostringstream req;
    req << "GET " << parts.path << " HTTP/1.1\r\n";
    req << "Host: " << parts.host;
    if ((parts.tls && parts.port != 443) || (!parts.tls && parts.port != 80))
    {
      req << ":" << parts.port;
    }
    req << "\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << ws_key << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n";
    for (const auto& h : headers)
    {
      req << h << "\r\n";
    }
    req << "\r\n";

    std::string req_str = req.str();
    if (!send_all(ssl, sock, req_str.data(), req_str.size()))
    {
      cleanup_ssl();
      close_socket(sock);
      sock = INVALID_SOCK;
      throw std::runtime_error("Failed to send WebSocket upgrade request");
    }

    // Read HTTP response
    std::string response;
    char ch;
    while (true)
    {
      if (!read_exact(ssl, sock, &ch, 1))
      {
        cleanup_ssl();
        close_socket(sock);
        sock = INVALID_SOCK;
        throw std::runtime_error("Failed to read WebSocket upgrade response");
      }
      response += ch;
      if (response.size() >= 4 &&
          response.substr(response.size() - 4) == "\r\n\r\n")
      {
        break;
      }
      if (response.size() > 8192)
      {
        cleanup_ssl();
        close_socket(sock);
        sock = INVALID_SOCK;
        throw std::runtime_error("WebSocket upgrade response too large");
      }
    }

    if (response.find("101") == std::string::npos)
    {
      cleanup_ssl();
      close_socket(sock);
      sock = INVALID_SOCK;
      throw std::runtime_error("WebSocket upgrade failed: " + response.substr(0, 80));
    }

    connected.store(true);

    // Start receive thread
    recv_thread = std::thread([this]() { receive_loop(); });
  }

  void send_frame(uint8_t opcode, const void* data, size_t len)
  {
    std::lock_guard<std::mutex> lock(send_mutex);
    if (!connected.load())
    {
      return;
    }

    std::vector<uint8_t> frame;
    // FIN + opcode
    frame.push_back(0x80 | opcode);

    // Mask bit + length
    if (len < 126)
    {
      frame.push_back(0x80 | static_cast<uint8_t>(len));
    }
    else if (len < 65536)
    {
      frame.push_back(0x80 | 126);
      frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
      frame.push_back(static_cast<uint8_t>(len & 0xFF));
    }
    else
    {
      frame.push_back(0x80 | 127);
      for (int i = 7; i >= 0; --i)
      {
        frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
      }
    }

    // Masking key
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dist(0, 255);
    std::array<uint8_t, 4> mask;
    for (auto& b : mask)
    {
      b = dist(gen);
    }
    frame.insert(frame.end(), mask.begin(), mask.end());

    // Masked payload
    auto* payload = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i)
    {
      frame.push_back(payload[i] ^ mask[i % 4]);
    }

    send_all(ssl, sock, frame.data(), frame.size());
  }

  void receive_loop()
  {
    while (connected.load())
    {
      uint8_t header[2];
      if (!read_exact(ssl, sock, header, 2))
      {
        break;
      }

      bool fin = (header[0] & 0x80) != 0;
      uint8_t opcode = header[0] & 0x0F;
      bool masked = (header[1] & 0x80) != 0;
      uint64_t payload_len = header[1] & 0x7F;

      if (payload_len == 126)
      {
        uint8_t ext[2];
        if (!read_exact(ssl, sock, ext, 2))
        {
          break;
        }
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
      }
      else if (payload_len == 127)
      {
        uint8_t ext[8];
        if (!read_exact(ssl, sock, ext, 8))
        {
          break;
        }
        payload_len = 0;
        for (int i = 0; i < 8; ++i)
        {
          payload_len = (payload_len << 8) | ext[i];
        }
      }

      std::array<uint8_t, 4> mask_key{};
      if (masked)
      {
        if (!read_exact(ssl, sock, mask_key.data(), 4))
        {
          break;
        }
      }

      std::vector<uint8_t> payload(payload_len);
      if (payload_len > 0)
      {
        if (!read_exact(ssl, sock, payload.data(), payload_len))
        {
          break;
        }
        if (masked)
        {
          for (size_t i = 0; i < payload_len; ++i)
          {
            payload[i] ^= mask_key[i % 4];
          }
        }
      }

      (void)fin;  // We handle complete frames for simplicity

      switch (opcode)
      {
      case WS_OP_TEXT:
      {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (on_message)
        {
          on_message(std::string(payload.begin(), payload.end()));
        }
        break;
      }
      case WS_OP_BINARY:
      {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (on_binary)
        {
          on_binary(payload);
        }
        break;
      }
      case WS_OP_PING:
      {
        send_frame(WS_OP_PONG, payload.data(), payload.size());
        break;
      }
      case WS_OP_PONG:
        break;
      case WS_OP_CLOSE:
      {
        int code = 1000;
        std::string reason;
        if (payload.size() >= 2)
        {
          code = (payload[0] << 8) | payload[1];
          if (payload.size() > 2)
          {
            reason.assign(payload.begin() + 2, payload.end());
          }
        }
        // Send close frame back
        if (connected.load())
        {
          uint8_t close_payload[2] = {
              static_cast<uint8_t>((code >> 8) & 0xFF),
              static_cast<uint8_t>(code & 0xFF)};
          send_frame(WS_OP_CLOSE, close_payload, 2);
        }
        connected.store(false);
        {
          std::lock_guard<std::mutex> lock(callback_mutex);
          if (on_close)
          {
            on_close(code, reason);
          }
        }
        return;
      }
      default:
        break;
      }
    }

    if (connected.exchange(false))
    {
      std::lock_guard<std::mutex> lock(callback_mutex);
      if (on_error)
      {
        on_error("WebSocket connection lost");
      }
    }
  }

  void close_connection(int code, const std::string& reason)
  {
    if (connected.exchange(false))
    {
      std::vector<uint8_t> payload;
      payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
      payload.push_back(static_cast<uint8_t>(code & 0xFF));
      payload.insert(payload.end(), reason.begin(), reason.end());
      send_frame(WS_OP_CLOSE, payload.data(), payload.size());
    }

    // Shutdown socket to unblock recv
    if (sock != INVALID_SOCK)
    {
#ifdef _WIN32
      shutdown(sock, SD_BOTH);
#else
      shutdown(sock, SHUT_RDWR);
#endif
    }

    if (recv_thread.joinable())
    {
      recv_thread.join();
    }

    cleanup_ssl();
    if (sock != INVALID_SOCK)
    {
      close_socket(sock);
      sock = INVALID_SOCK;
    }
  }

  void cleanup_ssl()
  {
    if (ssl)
    {
      SSL_shutdown(ssl);
      SSL_free(ssl);
      ssl = nullptr;
    }
    if (ssl_ctx)
    {
      SSL_CTX_free(ssl_ctx);
      ssl_ctx = nullptr;
    }
  }
};

WebSocketClient::WebSocketClient() : impl_(std::make_unique<Impl>()) {}
WebSocketClient::~WebSocketClient() = default;

void WebSocketClient::connect(const std::string& url,
                               const std::vector<std::string>& headers)
{
#ifdef _WIN32
  (void)ensure_winsock();
#endif
  impl_->connect(url, headers);
}

void WebSocketClient::send_text(const std::string& message)
{
  impl_->send_frame(WS_OP_TEXT, message.data(), message.size());
}

void WebSocketClient::send_binary(const std::vector<uint8_t>& data)
{
  impl_->send_frame(WS_OP_BINARY, data.data(), data.size());
}

void WebSocketClient::close(int code, const std::string& reason)
{
  impl_->close_connection(code, reason);
}

bool WebSocketClient::is_connected() const
{
  return impl_->connected.load();
}

void WebSocketClient::set_on_message(MessageCallback cb)
{
  std::lock_guard<std::mutex> lock(impl_->callback_mutex);
  impl_->on_message = std::move(cb);
}

void WebSocketClient::set_on_binary(BinaryCallback cb)
{
  std::lock_guard<std::mutex> lock(impl_->callback_mutex);
  impl_->on_binary = std::move(cb);
}

void WebSocketClient::set_on_error(ErrorCallback cb)
{
  std::lock_guard<std::mutex> lock(impl_->callback_mutex);
  impl_->on_error = std::move(cb);
}

void WebSocketClient::set_on_close(CloseCallback cb)
{
  std::lock_guard<std::mutex> lock(impl_->callback_mutex);
  impl_->on_close = std::move(cb);
}
}  // namespace neamc::llm
