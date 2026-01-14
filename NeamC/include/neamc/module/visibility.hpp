//
// NeamC - Module visibility rules
//

#pragma once

#include <string_view>

namespace neamc::module
{
enum class VisibilityLevel
{
  kPrivate,
  kPublic,
  kCrate,
  kSuper
};

bool is_accessible(std::string_view from_module, std::string_view target_module,
                   VisibilityLevel visibility);
}  // namespace neamc::module
