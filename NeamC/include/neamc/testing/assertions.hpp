//
// NeamC - Testing assertions
//

#pragma once

#include <sstream>
#include <string>
#include <type_traits>

#include "neamc/diagnostic.hpp"
#include "neamc/stdlib/option.hpp"
#include "neamc/stdlib/result.hpp"

namespace neamc::testing
{
class AssertionFailure : public std::exception
{
public:
  AssertionFailure(std::string message, SourceLocation loc);
  const char* what() const noexcept override;
  const std::string& message() const;
  const SourceLocation& location() const;

private:
  std::string message_;
  SourceLocation location_;
};

#define NEAM_SOURCE_LOC ::neamc::SourceLocation{__FILE__, __LINE__, 0}

#define assert_eq(left, right) \
  ::neamc::testing::impl::assert_eq_impl((left), (right), #left, #right, NEAM_SOURCE_LOC)

#define assert_ne(left, right) \
  ::neamc::testing::impl::assert_ne_impl((left), (right), #left, #right, NEAM_SOURCE_LOC)

#define assert_true(expr) \
  ::neamc::testing::impl::assert_true_impl((expr), #expr, NEAM_SOURCE_LOC)

#define assert_false(expr) \
  ::neamc::testing::impl::assert_false_impl((expr), #expr, NEAM_SOURCE_LOC)

#define assert_some(opt) \
  ::neamc::testing::impl::assert_some_impl((opt), #opt, NEAM_SOURCE_LOC)

#define assert_none(opt) \
  ::neamc::testing::impl::assert_none_impl((opt), #opt, NEAM_SOURCE_LOC)

#define assert_ok(result) \
  ::neamc::testing::impl::assert_ok_impl((result), #result, NEAM_SOURCE_LOC)

#define assert_err(result) \
  ::neamc::testing::impl::assert_err_impl((result), #result, NEAM_SOURCE_LOC)

namespace impl
{

template <typename T, typename U>
void assert_eq_impl(const T& left, const U& right, const char* left_expr, const char* right_expr,
                    SourceLocation loc)
{
  if (!(left == right))
  {
    std::ostringstream msg;
    msg << "Assertion failed: " << left_expr << " == " << right_expr << "\n";
    msg << "  Left:  " << left << "\n";
    msg << "  Right: " << right;
    throw AssertionFailure(msg.str(), loc);
  }
}

template <typename T, typename U>
void assert_ne_impl(const T& left, const U& right, const char* left_expr, const char* right_expr,
                    SourceLocation loc)
{
  if (left == right)
  {
    std::ostringstream msg;
    msg << "Assertion failed: " << left_expr << " != " << right_expr << "\n";
    msg << "  Both equal to: " << left;
    throw AssertionFailure(msg.str(), loc);
  }
}

void assert_true_impl(bool value, const char* expr, SourceLocation loc);
void assert_false_impl(bool value, const char* expr, SourceLocation loc);

template <typename T>
void assert_some_impl(const stdlib::Option<T>& opt, const char* expr, SourceLocation loc)
{
  if (opt.is_none())
  {
    throw AssertionFailure(std::string("Expected Some, got None for: ") + expr, loc);
  }
}

template <typename T>
void assert_none_impl(const stdlib::Option<T>& opt, const char* expr, SourceLocation loc)
{
  if (opt.is_some())
  {
    throw AssertionFailure(std::string("Expected None, got Some for: ") + expr, loc);
  }
}

template <typename T, typename E>
void assert_ok_impl(const stdlib::Result<T, E>& result, const char* expr, SourceLocation loc)
{
  if (result.is_err())
  {
    std::ostringstream msg;
    msg << "Expected Ok, got Err for: " << expr;
    throw AssertionFailure(msg.str(), loc);
  }
}

template <typename T, typename E>
void assert_err_impl(const stdlib::Result<T, E>& result, const char* expr, SourceLocation loc)
{
  if (result.is_ok())
  {
    throw AssertionFailure(std::string("Expected Err, got Ok for: ") + expr, loc);
  }
}

}  // namespace impl
}  // namespace neamc::testing
