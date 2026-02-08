//
// Neam LLM - Structured logging for LLM stack
//
// Log levels controlled via NEAM_LOG_LEVEL env var (DEBUG, INFO, WARN, ERROR).
// Default: WARN. All output goes to stderr to avoid interfering with agent output.
//

#pragma once

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

// Windows headers define ERROR as a macro; undefine to avoid conflicts.
#ifdef ERROR
#undef ERROR
#endif

namespace neamc::llm
{

enum class LogLevel
{
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3
};

class LLMLogger
{
public:
  static LogLevel level()
  {
    static const LogLevel lvl = parse_env();
    return lvl;
  }

  static void log(LogLevel lvl, const std::string& component, const std::string& message)
  {
    if (lvl < level())
    {
      return;
    }
    auto now = std::chrono::system_clock::now();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::cerr << "[" << ms << "] [" << level_str(lvl) << "] [" << component << "] " << message
              << "\n";
  }

  static void debug(const std::string& component, const std::string& message)
  {
    log(LogLevel::DEBUG, component, message);
  }

  static void info(const std::string& component, const std::string& message)
  {
    log(LogLevel::INFO, component, message);
  }

  static void warn(const std::string& component, const std::string& message)
  {
    log(LogLevel::WARN, component, message);
  }

  static void error(const std::string& component, const std::string& message)
  {
    log(LogLevel::ERROR, component, message);
  }

private:
  static const char* level_str(LogLevel lvl)
  {
    switch (lvl)
    {
      case LogLevel::DEBUG:
        return "DEBUG";
      case LogLevel::INFO:
        return "INFO";
      case LogLevel::WARN:
        return "WARN";
      case LogLevel::ERROR:
        return "ERROR";
    }
    return "UNKNOWN";
  }

  static LogLevel parse_env()
  {
    const char* env = std::getenv("NEAM_LOG_LEVEL");
    if (!env)
    {
      return LogLevel::WARN;
    }
    std::string s(env);
    if (s == "DEBUG" || s == "debug")
      return LogLevel::DEBUG;
    if (s == "INFO" || s == "info")
      return LogLevel::INFO;
    if (s == "ERROR" || s == "error")
      return LogLevel::ERROR;
    return LogLevel::WARN;
  }
};

}  // namespace neamc::llm
