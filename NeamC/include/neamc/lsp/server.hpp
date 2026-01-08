//
// Neam LSP - Language Server Protocol implementation
//

#pragma once

#include <string>
#include <unordered_map>

#include "neamc/ast.hpp"

namespace neamc::lsp
{
class Server
{
public:
  void run();

private:
  struct DocumentState
  {
    std::string text;
    Program program{};
    bool parsed = false;
    std::string parse_error;
  };

  std::unordered_map<std::string, DocumentState> documents_{};

  void handle_message(const std::string& payload);
  void handle_initialize(int id);
  void handle_did_open(const std::string& uri, const std::string& text);
  void handle_did_change(const std::string& uri, const std::string& text);
  void handle_completion(int id, const std::string& uri, std::size_t line,
                         std::size_t character);
  void handle_semantic_tokens(int id, const std::string& uri);
  void handle_hover(int id, const std::string& uri, std::size_t line, std::size_t character);

  void publish_diagnostics(const std::string& uri, const DocumentState& state);

  static std::size_t offset_at(const std::string& text, std::size_t line, std::size_t character);
  static bool read_message(std::string& payload);
  static void write_message(const std::string& payload);
};
}  // namespace neamc::lsp
