//
// Neam LLM - Message codec helpers
//

#include "neamc/llm/message_codec.hpp"

#include <stdexcept>

namespace neamc::llm
{
namespace
{
std::string extract_string_value(const neamc::vm::Value& value)
{
  if (!value.is_string())
  {
    throw std::runtime_error("Expected string value in message map");
  }
  auto* str = neamc::vm::as_string(value);
  return std::string(str->chars, str->length);
}
}

nlohmann::json objlist_to_messages_json(const neamc::vm::ObjList* list)
{
  if (!list)
  {
    return nlohmann::json::array();
  }
  nlohmann::json messages = nlohmann::json::array();
  for (const auto& item : list->items)
  {
    if (!item.is_map())
    {
      throw std::runtime_error("Expected map entries for message list");
    }
    auto* map = neamc::vm::as_map(item);
    const auto role_it = map->entries.find("role");
    const auto content_it = map->entries.find("content");
    if (role_it == map->entries.end() || content_it == map->entries.end())
    {
      throw std::runtime_error("Message map missing role/content");
    }
    messages.push_back({{"role", extract_string_value(role_it->second)},
                        {"content", extract_string_value(content_it->second)}});
  }
  return messages;
}

neamc::vm::ObjList* messages_json_to_objlist(const nlohmann::json& json)
{
  if (!json.is_array())
  {
    throw std::runtime_error("Expected JSON array for messages");
  }
  std::vector<neamc::vm::Value> items;
  items.reserve(json.size());
  for (const auto& entry : json)
  {
    if (!entry.is_object())
    {
      throw std::runtime_error("Expected JSON object for message entry");
    }
    const auto role = entry.value("role", "");
    const auto content = entry.value("content", "");
    std::unordered_map<std::string, neamc::vm::Value> map;
    map.emplace("role", neamc::vm::Value::String(role.c_str(), role.size()));
    map.emplace("content",
                neamc::vm::Value::String(content.c_str(), content.size()));
    items.push_back(neamc::vm::Value::Map(neamc::vm::new_map(std::move(map))));
  }
  return neamc::vm::new_list(std::move(items));
}
}  // namespace neamc::llm
