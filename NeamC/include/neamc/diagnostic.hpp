// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Diagnostic helpers
//

#pragma once

#include <string>

namespace neamc
{
struct SourceLocation
{
  std::string file;
  std::size_t line{0};
  std::size_t column{0};

  SourceLocation() = default;
  SourceLocation(std::string file_in, std::size_t line_in, std::size_t column_in)
      : file(std::move(file_in)), line(line_in), column(column_in)
  {
  }
};
}  // namespace neamc
