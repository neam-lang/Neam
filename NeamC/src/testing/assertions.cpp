//
// NeamC - Assertion implementation
//

#include "neamc/testing/assertions.hpp"

#include <utility>

namespace neamc::testing
{
AssertionFailure::AssertionFailure(std::string message, SourceLocation loc)
    : message_(std::move(message)), location_(std::move(loc))
{
}

const char* AssertionFailure::what() const noexcept
{
  return message_.c_str();
}

const std::string& AssertionFailure::message() const
{
  return message_;
}

const SourceLocation& AssertionFailure::location() const
{
  return location_;
}

namespace impl
{
void assert_true_impl(bool value, const char* expr, SourceLocation loc)
{
  if (!value)
  {
    throw AssertionFailure(std::string("Expected true for: ") + expr, loc);
  }
}

void assert_false_impl(bool value, const char* expr, SourceLocation loc)
{
  if (value)
  {
    throw AssertionFailure(std::string("Expected false for: ") + expr, loc);
  }
}

}  // namespace impl
}  // namespace neamc::testing
