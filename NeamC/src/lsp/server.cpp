//
// Neam LSP - Language Server Protocol implementation
//

#include "neamc/lsp/server.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "neamc/parser.hpp"

#include <nlohmann/json.hpp>

namespace neamc::lsp
{
namespace
{
constexpr const char* kContentLengthHeader = "Content-Length:";

std::string trim(const std::string& value)
{
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
  {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
  {
    --end;
  }
  return value.substr(start, end - start);
}

std::vector<int> build_semantic_tokens(const std::string& text)
{
  const std::unordered_set<std::string> keywords = {"agent", "skill", "fun", "fn"};
  const auto keyword_index = 0;
  const auto variable_index = 1;

  std::vector<int> data;
  std::size_t line = 0;
  std::size_t line_start = 0;
  int prev_line = 0;
  int prev_start = 0;

  for (std::size_t i = 0; i <= text.size(); ++i)
  {
    if (i == text.size() || text[i] == '\n')
    {
      std::size_t cursor = line_start;
      while (cursor < i)
      {
        while (cursor < i && !std::isalpha(static_cast<unsigned char>(text[cursor])) &&
               text[cursor] != '_')
        {
          ++cursor;
        }
        const std::size_t token_start = cursor;
        while (cursor < i &&
               (std::isalnum(static_cast<unsigned char>(text[cursor])) || text[cursor] == '_'))
        {
          ++cursor;
        }
        if (token_start < cursor)
        {
          const std::string token = text.substr(token_start, cursor - token_start);
          const int token_type = keywords.count(token) ? keyword_index : variable_index;
          const int delta_line = static_cast<int>(line) - prev_line;
          const int delta_start = delta_line == 0
                                     ? static_cast<int>(token_start - line_start) - prev_start
                                     : static_cast<int>(token_start - line_start);
          data.push_back(delta_line);
          data.push_back(delta_start);
          data.push_back(static_cast<int>(cursor - token_start));
          data.push_back(token_type);
          data.push_back(0);
          prev_line = static_cast<int>(line);
          prev_start = static_cast<int>(token_start - line_start);
        }
      }
      line_start = i + 1;
      ++line;
    }
  }
  return data;
}

nlohmann::json make_completion_item(const std::string& label, int kind = 1)
{
  return nlohmann::json{{"label", label}, {"kind", kind}};
}

std::string extract_make_prompt_preview(const std::string& line_text)
{
  const std::string needle = "make_prompt(";
  const auto pos = line_text.find(needle);
  if (pos == std::string::npos)
  {
    return {};
  }
  std::size_t cursor = pos + needle.size();
  while (cursor < line_text.size() && std::isspace(static_cast<unsigned char>(line_text[cursor])))
  {
    ++cursor;
  }
  if (cursor >= line_text.size() || line_text[cursor] != '"')
  {
    return {};
  }
  ++cursor;
  std::ostringstream out;
  while (cursor < line_text.size() && line_text[cursor] != '"')
  {
    out << line_text[cursor++];
  }
  return out.str();
}
}  // namespace

void Server::run()
{
  std::string payload;
  while (read_message(payload))
  {
    handle_message(payload);
  }
}

void Server::handle_message(const std::string& payload)
{
  nlohmann::json message = nlohmann::json::parse(payload, nullptr, false);
  if (message.is_discarded())
  {
    return;
  }
  const auto method_it = message.find("method");
  if (method_it == message.end())
  {
    return;
  }
  const std::string method = method_it->get<std::string>();
  const auto id_it = message.find("id");
  const int id = id_it != message.end() ? id_it->get<int>() : -1;

  if (method == "initialize" && id_it != message.end())
  {
    handle_initialize(id);
    return;
  }
  if (method == "textDocument/didOpen")
  {
    const auto& params = message["params"]["textDocument"];
    handle_did_open(params["uri"].get<std::string>(), params["text"].get<std::string>());
    return;
  }
  if (method == "textDocument/didChange")
  {
    const auto& params = message["params"];
    const auto& changes = params["contentChanges"];
    if (!changes.empty())
    {
      handle_did_change(params["textDocument"]["uri"].get<std::string>(),
                        changes[0]["text"].get<std::string>());
    }
    return;
  }
  if (method == "textDocument/completion" && id_it != message.end())
  {
    const auto& params = message["params"];
    handle_completion(id, params["textDocument"]["uri"].get<std::string>(),
                      params["position"]["line"].get<std::size_t>(),
                      params["position"]["character"].get<std::size_t>());
    return;
  }
  if (method == "textDocument/semanticTokens/full" && id_it != message.end())
  {
    handle_semantic_tokens(id, message["params"]["textDocument"]["uri"].get<std::string>());
    return;
  }
  if (method == "textDocument/hover" && id_it != message.end())
  {
    const auto& params = message["params"];
    handle_hover(id, params["textDocument"]["uri"].get<std::string>(),
                 params["position"]["line"].get<std::size_t>(),
                 params["position"]["character"].get<std::size_t>());
    return;
  }
}

void Server::handle_initialize(int id)
{
  nlohmann::json result = {
      {"capabilities",
       {{"textDocumentSync", 1},
        {"completionProvider", {{"triggerCharacters", {".", ":"}}}},
        {"semanticTokensProvider",
         {{"legend", {{"tokenTypes", {"keyword", "variable"}}, {"tokenModifiers", nlohmann::json::array()}}},
          {"full", true}}},
        {"hoverProvider", true}}}};

  nlohmann::json response = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
  write_message(response.dump());
}

void Server::handle_did_open(const std::string& uri, const std::string& text)
{
  handle_did_change(uri, text);
}

void Server::handle_did_change(const std::string& uri, const std::string& text)
{
  DocumentState& state = documents_[uri];
  state.text = text;
  state.parsed = false;
  state.parse_error.clear();

  try
  {
    Parser parser(text);
    state.program = parser.parse();
    state.parsed = true;
  }
  catch (const std::exception& ex)
  {
    state.parse_error = ex.what();
  }

  publish_diagnostics(uri, state);
}

void Server::handle_completion(int id, const std::string& uri, std::size_t line,
                               std::size_t character)
{
  nlohmann::json result = nlohmann::json::array();
  auto it = documents_.find(uri);
  if (it != documents_.end())
  {
    const auto offset = offset_at(it->second.text, line, character);
    const std::string prefix = it->second.text.substr(0, offset);
    if (prefix.size() >= 4 && prefix.compare(prefix.size() - 4, 4, "std.") == 0)
    {
      result.push_back(make_completion_item("std.agent"));
      result.push_back(make_completion_item("std.fs"));
      result.push_back(make_completion_item("std.net"));
    }
    else
    {
      const auto dot_pos = prefix.find_last_of('.');
      if (dot_pos != std::string::npos && dot_pos + 1 == prefix.size())
      {
        result.push_back(make_completion_item("skills"));
        result.push_back(make_completion_item("model"));
        result.push_back(make_completion_item("system"));
        result.push_back(make_completion_item("context"));
        result.push_back(make_completion_item("provider"));
        result.push_back(make_completion_item("name"));
      }
    }
  }
  nlohmann::json response = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
  write_message(response.dump());
}

void Server::handle_semantic_tokens(int id, const std::string& uri)
{
  nlohmann::json result = {{"data", nlohmann::json::array()}};
  auto it = documents_.find(uri);
  if (it != documents_.end())
  {
    auto data = build_semantic_tokens(it->second.text);
    result["data"] = data;
  }
  nlohmann::json response = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
  write_message(response.dump());
}

void Server::handle_hover(int id, const std::string& uri, std::size_t line,
                          std::size_t character)
{
  nlohmann::json result = nullptr;
  auto it = documents_.find(uri);
  if (it != documents_.end())
  {
    const auto offset = offset_at(it->second.text, line, character);
    std::size_t line_start = offset;
    while (line_start > 0 && it->second.text[line_start - 1] != '\n')
    {
      --line_start;
    }
    std::size_t line_end = offset;
    while (line_end < it->second.text.size() && it->second.text[line_end] != '\n')
    {
      ++line_end;
    }
    const std::string line_text = it->second.text.substr(line_start, line_end - line_start);
    const auto preview = extract_make_prompt_preview(line_text);
    if (!preview.empty())
    {
      result = {{"contents", {{"kind", "markdown"}, {"value", "Prompt preview:\n\n" + preview}}}};
    }
  }

  nlohmann::json response = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
  write_message(response.dump());
}

void Server::publish_diagnostics(const std::string& uri, const DocumentState& state)
{
  nlohmann::json diagnostics = nlohmann::json::array();
  if (!state.parsed)
  {
    diagnostics.push_back(
        {{"range", {{"start", {{"line", 0}, {"character", 0}}},
                    {"end", {{"line", 0}, {"character", 1}}}}},
         {"severity", 1},
         {"source", "neam-lsp"},
         {"message", state.parse_error.empty() ? "Parse error" : state.parse_error}});
  }
  else
  {
    std::unordered_set<std::string> skills;
    std::unordered_set<std::string> knowledge_bases;
    for (const auto& stmt : state.program.statements)
    {
      if (const auto* skill = std::get_if<SkillDecl>(&stmt->node))
      {
        skills.insert(skill->name);
      }
      if (const auto* knowledge = std::get_if<KnowledgeDecl>(&stmt->node))
      {
        knowledge_bases.insert(knowledge->name);
      }
    }
    for (const auto& stmt : state.program.statements)
    {
      if (const auto* agent = std::get_if<AgentDecl>(&stmt->node))
      {
        for (const auto& skill_ref : agent->skills)
        {
          if (skills.count(skill_ref.name) == 0)
          {
            const std::size_t length = skill_ref.span.length > 0 ? skill_ref.span.length : 1;
            diagnostics.push_back(
                {{"range",
                  {{"start", {{"line", skill_ref.span.line}, {"character", skill_ref.span.column}}},
                   {"end", {{"line", skill_ref.span.line},
                            {"character", skill_ref.span.column + length}}}}},
                 {"severity", 1},
                 {"source", "neam-lsp"},
                 {"message", "Undefined skill: " + skill_ref.name}});
          }
        }
        for (const auto& kb_ref : agent->connected_knowledge)
        {
          if (knowledge_bases.count(kb_ref.name) == 0)
          {
            const std::size_t length = kb_ref.span.length > 0 ? kb_ref.span.length : 1;
            diagnostics.push_back(
                {{"range",
                  {{"start", {{"line", kb_ref.span.line}, {"character", kb_ref.span.column}}},
                   {"end", {{"line", kb_ref.span.line},
                            {"character", kb_ref.span.column + length}}}}},
                 {"severity", 1},
                 {"source", "neam-lsp"},
                 {"message", "Undefined knowledge base: " + kb_ref.name}});
          }
        }
      }
    }
  }

  nlohmann::json notification = {{"jsonrpc", "2.0"},
                                 {"method", "textDocument/publishDiagnostics"},
                                 {"params", {{"uri", uri}, {"diagnostics", diagnostics}}}};
  write_message(notification.dump());
}

std::size_t Server::offset_at(const std::string& text, std::size_t line, std::size_t character)
{
  std::size_t offset = 0;
  std::size_t current_line = 0;
  while (offset < text.size() && current_line < line)
  {
    if (text[offset] == '\n')
    {
      ++current_line;
    }
    ++offset;
  }
  return std::min(offset + character, text.size());
}

bool Server::read_message(std::string& payload)
{
  payload.clear();
  std::string line;
  std::size_t content_length = 0;
  while (std::getline(std::cin, line))
  {
    if (line == "\r" || line.empty())
    {
      break;
    }
    if (line.rfind(kContentLengthHeader, 0) == 0)
    {
      const auto value = trim(line.substr(std::strlen(kContentLengthHeader)));
      content_length = static_cast<std::size_t>(std::stoul(value));
    }
  }
  if (content_length == 0)
  {
    return false;
  }
  payload.resize(content_length);
  std::cin.read(payload.data(), static_cast<std::streamsize>(content_length));
  return std::cin.good();
}

void Server::write_message(const std::string& payload)
{
  std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
  std::cout.flush();
}
}  // namespace neamc::lsp
