//
// Neam Virtual Machine - Runtime errors
//

#pragma once

#include <stdexcept>
#include <string>

namespace neamc::vm
{
enum class RuntimeErrorCode
{
  Timeout = 5000,
  IoError = 5001,
  Cancelled = 5002,
  NetworkError = 5003
};

class RuntimeError : public std::runtime_error
{
public:
  RuntimeError(RuntimeErrorCode code, const std::string& message)
      : std::runtime_error(message), code_(code)
  {
  }

  RuntimeErrorCode code() const { return code_; }

private:
  RuntimeErrorCode code_;
};
}  // namespace neamc::vm
