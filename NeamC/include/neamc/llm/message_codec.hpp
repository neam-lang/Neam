// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Message codec helpers
//

#pragma once

#include <nlohmann/json.hpp>

#include "neamc/vm/object.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::llm
{
nlohmann::json objlist_to_messages_json(const neamc::vm::ObjList* list);

neamc::vm::ObjList* messages_json_to_objlist(const nlohmann::json& json);
}  // namespace neamc::llm
