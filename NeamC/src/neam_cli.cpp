// SPDX-License-Identifier: Apache-2.0
//
// Neam CLI - Interactive REPL (Read-Eval-Print Loop)
//
// Usage:
//   neam-cli              Start interactive REPL
//   neam-cli <file.neam>  Execute a file then enter REPL
//   neam-cli -e "code"    Execute code and exit
//

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#include "neamc/compiler.hpp"
#include "neamc/parser.hpp"
#include "neamc/vm/object.hpp"
#include "neamc/vm/vm.hpp"

namespace
{
constexpr const char* kVersion = "0.6.0";
constexpr std::size_t kMaxHistorySize = 100;

// ANSI escape codes
namespace ansi
{
constexpr const char* reset = "\033[0m";
constexpr const char* bold = "\033[1m";
constexpr const char* dim = "\033[2m";
constexpr const char* italic = "\033[3m";
constexpr const char* underline = "\033[4m";
constexpr const char* red = "\033[31m";
constexpr const char* green = "\033[32m";
constexpr const char* yellow = "\033[33m";
constexpr const char* blue = "\033[34m";
constexpr const char* magenta = "\033[35m";
constexpr const char* cyan = "\033[36m";
constexpr const char* white = "\033[37m";
constexpr const char* clear_screen = "\033[2J\033[H";
constexpr const char* clear_line = "\033[2K\r";
constexpr const char* cursor_left = "\033[D";
constexpr const char* cursor_right = "\033[C";
constexpr const char* save_cursor = "\033[s";
constexpr const char* restore_cursor = "\033[u";
}  // namespace ansi

// Check if stdout is a terminal
bool is_tty()
{
  return isatty(fileno(stdout)) != 0 && isatty(fileno(stdin)) != 0;
}

bool use_colors = false;
bool use_raw_mode = false;

std::string colorize(const char* color_code, const std::string& text)
{
  if (!use_colors)
  {
    return text;
  }
  return std::string(color_code) + text + ansi::reset;
}

//
// Terminal handling for line editing
//
#ifndef _WIN32
class Terminal
{
public:
  Terminal() : raw_mode_(false) {}

  ~Terminal()
  {
    disable_raw_mode();
  }

  bool enable_raw_mode()
  {
    if (!isatty(STDIN_FILENO)) return false;
    if (raw_mode_) return true;

    if (tcgetattr(STDIN_FILENO, &orig_termios_) == -1) return false;

    struct termios raw = orig_termios_;
    // Input: no break, no CR to NL, no parity check, no strip, no flow ctrl
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // Output: disable post processing (we handle it ourselves)
    // raw.c_oflag &= ~(OPOST);  // Keep OPOST for proper newline handling
    // Control: set 8 bit chars
    raw.c_cflag |= (CS8);
    // Local: no echo, no canonical, no extended, no signal chars
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    // Return after 1 byte, no timeout
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return false;

    raw_mode_ = true;
    return true;
  }

  void disable_raw_mode()
  {
    if (raw_mode_)
    {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios_);
      raw_mode_ = false;
    }
  }

  bool is_raw() const { return raw_mode_; }

  int read_key()
  {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
      if (nread == -1 && errno != EAGAIN) return -1;
    }

    // Handle escape sequences
    if (c == '\x1b')
    {
      char seq[3];
      if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
      if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

      if (seq[0] == '[')
      {
        switch (seq[1])
        {
          case 'A': return KEY_UP;
          case 'B': return KEY_DOWN;
          case 'C': return KEY_RIGHT;
          case 'D': return KEY_LEFT;
          case 'H': return KEY_HOME;
          case 'F': return KEY_END;
          case '3':
            if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~')
              return KEY_DELETE;
            break;
          case '1': case '7':
            if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~')
              return KEY_HOME;
            break;
          case '4': case '8':
            if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~')
              return KEY_END;
            break;
        }
      }
      else if (seq[0] == 'O')
      {
        switch (seq[1])
        {
          case 'H': return KEY_HOME;
          case 'F': return KEY_END;
        }
      }
      return '\x1b';
    }

    return c;
  }

  // Special key codes
  static constexpr int KEY_UP = 1000;
  static constexpr int KEY_DOWN = 1001;
  static constexpr int KEY_LEFT = 1002;
  static constexpr int KEY_RIGHT = 1003;
  static constexpr int KEY_HOME = 1004;
  static constexpr int KEY_END = 1005;
  static constexpr int KEY_DELETE = 1006;
  static constexpr int KEY_BACKSPACE = 127;
  static constexpr int KEY_ENTER = 13;
  static constexpr int KEY_TAB = 9;
  static constexpr int KEY_CTRL_A = 1;
  static constexpr int KEY_CTRL_B = 2;
  static constexpr int KEY_CTRL_C = 3;
  static constexpr int KEY_CTRL_D = 4;
  static constexpr int KEY_CTRL_E = 5;
  static constexpr int KEY_CTRL_F = 6;
  static constexpr int KEY_CTRL_K = 11;
  static constexpr int KEY_CTRL_L = 12;
  static constexpr int KEY_CTRL_N = 14;
  static constexpr int KEY_CTRL_P = 16;
  static constexpr int KEY_CTRL_U = 21;
  static constexpr int KEY_CTRL_W = 23;

private:
  struct termios orig_termios_;
  bool raw_mode_;
};

#else
// Windows implementation
class Terminal
{
public:
  Terminal() : raw_mode_(false) {}
  ~Terminal() { disable_raw_mode(); }

  bool enable_raw_mode()
  {
    // Windows console mode handling would go here
    return false; // Simplified: fall back to basic mode on Windows
  }

  void disable_raw_mode() {}
  bool is_raw() const { return false; }
  int read_key() { return _getch(); }

  static constexpr int KEY_UP = 1000;
  static constexpr int KEY_DOWN = 1001;
  static constexpr int KEY_LEFT = 1002;
  static constexpr int KEY_RIGHT = 1003;
  static constexpr int KEY_HOME = 1004;
  static constexpr int KEY_END = 1005;
  static constexpr int KEY_DELETE = 1006;
  static constexpr int KEY_BACKSPACE = 8;
  static constexpr int KEY_ENTER = 13;
  static constexpr int KEY_TAB = 9;
  static constexpr int KEY_CTRL_A = 1;
  static constexpr int KEY_CTRL_B = 2;
  static constexpr int KEY_CTRL_C = 3;
  static constexpr int KEY_CTRL_D = 4;
  static constexpr int KEY_CTRL_E = 5;
  static constexpr int KEY_CTRL_F = 6;
  static constexpr int KEY_CTRL_K = 11;
  static constexpr int KEY_CTRL_L = 12;
  static constexpr int KEY_CTRL_N = 14;
  static constexpr int KEY_CTRL_P = 16;
  static constexpr int KEY_CTRL_U = 21;
  static constexpr int KEY_CTRL_W = 23;

private:
  bool raw_mode_;
};
#endif

//
// Suggestion Engine - provides autocomplete suggestions
//
class SuggestionEngine
{
public:
  SuggestionEngine()
  {
    // REPL commands
    commands_ = {
      ":help", ":h", ":?",
      ":quit", ":q", ":exit",
      ":version", ":v",
      ":load ", ":l ",
      ":reload", ":r",
      ":save ", ":s ",
      ":clear", ":c",
      ":cls",
      ":vars",
      ":history", ":hist",
      ":!!",
      ":type ", ":t ",
      ":time "
    };

    // Neam language keywords
    keywords_ = {
      "let ", "fun", "return ", "emit ",
      "if ", "else ", "while ", "for ",
      "true", "false", "nil",
      "agent ", "skill ", "knowledge ",
      "prompt ", "system:", "provider:", "model:",
      "description:", "params:", "impl",
      "try ", "catch ", "finally ",
      "import ", "from ", "as ",
      "async ", "await ", "spawn ",
      "match ", "case ", "default:",
      "break", "continue",
      "and ", "or ", "not "
    };

    // Built-in functions and common patterns
    builtins_ = {
      "print(", "println(", "len(", "type(",
      "push(", "pop(", "keys(", "values(",
      "str(", "num(", "bool(",
      "range(", "map(", "filter(", "reduce(",
      "split(", "join(", "trim(",
      "read_file(", "write_file(",
      "http_get(", "http_post(",
      "json_parse(", "json_stringify(",
      "sleep(", "now(", "date(",
      "random(", "floor(", "ceil(", "round(", "abs(",
      "sqrt(", "pow(", "sin(", "cos(", "tan(",
      "log(", "exp(",
      "contains(", "starts_with(", "ends_with(",
      "replace(", "substr(", "index_of(",
      "sort(", "reverse(", "concat(",
      "assert(", "panic("
    };

    // Common code patterns/snippets
    snippets_ = {
      "let x = ",
      "let result = ",
      "fun() { }",
      "fun(x) { return x; }",
      "if () { } else { }",
      "while () { }",
      "for (let i = 0; i < ; i = i + 1) { }",
      "agent Assistant {\n  provider: \"openai\"\n  model: \"gpt-4\"\n  system: \"\"\n}",
      "skill Tool {\n  description: \"\"\n  params: []\n  impl() { }\n}",
      "[].map(fun(x) { return x; })",
      "[].filter(fun(x) { return true; })",
      "try { } catch (e) { }"
    };
  }

  // Get suggestion for current input
  // Returns the FULL suggestion (not just the suffix)
  std::string get_suggestion(const std::string& input) const
  {
    if (input.empty()) return "";

    std::string trimmed = input;
    // Trim leading whitespace
    size_t start = trimmed.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    trimmed = trimmed.substr(start);

    // Check commands first (if starts with :)
    if (trimmed[0] == ':')
    {
      for (const auto& cmd : commands_)
      {
        if (cmd.size() > trimmed.size() &&
            cmd.substr(0, trimmed.size()) == trimmed)
        {
          return input + cmd.substr(trimmed.size());
        }
      }
      return "";
    }

    // Find the last "word" being typed
    std::string last_word = get_last_word(trimmed);
    if (last_word.empty()) return "";

    // Check keywords
    for (const auto& kw : keywords_)
    {
      if (kw.size() > last_word.size() &&
          kw.substr(0, last_word.size()) == last_word)
      {
        return input + kw.substr(last_word.size());
      }
    }

    // Check builtins
    for (const auto& fn : builtins_)
    {
      if (fn.size() > last_word.size() &&
          fn.substr(0, last_word.size()) == last_word)
      {
        return input + fn.substr(last_word.size());
      }
    }

    // Check user-defined symbols
    for (const auto& sym : user_symbols_)
    {
      if (sym.size() > last_word.size() &&
          sym.substr(0, last_word.size()) == last_word)
      {
        return input + sym.substr(last_word.size());
      }
    }

    return "";
  }

  // Add a user-defined symbol (variable, function name)
  void add_user_symbol(const std::string& symbol)
  {
    if (!symbol.empty() &&
        std::find(user_symbols_.begin(), user_symbols_.end(), symbol) == user_symbols_.end())
    {
      user_symbols_.push_back(symbol);
    }
  }

  // Clear user symbols (on :clear)
  void clear_user_symbols()
  {
    user_symbols_.clear();
  }

  // Get snippet suggestions for empty or simple inputs
  const std::vector<std::string>& get_snippets() const { return snippets_; }

private:
  std::string get_last_word(const std::string& input) const
  {
    if (input.empty()) return "";

    // Find the start of the last word
    size_t pos = input.size();
    while (pos > 0)
    {
      char c = input[pos - 1];
      // Word characters: alphanumeric, underscore, or starting special chars
      if (std::isalnum(c) || c == '_' || c == ':')
      {
        pos--;
      }
      else
      {
        break;
      }
    }
    return input.substr(pos);
  }

  std::vector<std::string> commands_;
  std::vector<std::string> keywords_;
  std::vector<std::string> builtins_;
  std::vector<std::string> snippets_;
  std::vector<std::string> user_symbols_;
};

// Global suggestion engine
SuggestionEngine g_suggestions;

//
// Line editor with history support
//
class LineEditor
{
public:
  LineEditor(Terminal& term, std::vector<std::string>& history)
      : term_(term), history_(history), history_index_(0) {}

  // Read a line with editing support
  // Returns empty optional on EOF (Ctrl+D)
  std::optional<std::string> read_line(const std::string& prompt)
  {
    if (!term_.is_raw())
    {
      // Fall back to simple getline
      std::cout << prompt << std::flush;
      std::string line;
      if (!std::getline(std::cin, line))
      {
        return std::nullopt;
      }
      return line;
    }

    line_.clear();
    cursor_pos_ = 0;
    history_index_ = history_.size();
    saved_line_.clear();

    std::cout << prompt << std::flush;

    while (true)
    {
      int c = term_.read_key();

      if (c == -1) continue;

      if (c == Terminal::KEY_ENTER || c == '\n' || c == '\r')
      {
        std::cout << "\n";
        return line_;
      }

      if (c == Terminal::KEY_CTRL_D)
      {
        if (line_.empty())
        {
          std::cout << "\n";
          return std::nullopt; // EOF
        }
        // Delete char under cursor
        delete_char();
        continue;
      }

      if (c == Terminal::KEY_CTRL_C)
      {
        std::cout << "^C\n";
        line_.clear();
        cursor_pos_ = 0;
        std::cout << prompt << std::flush;
        continue;
      }

      if (c == Terminal::KEY_CTRL_L)
      {
        // Clear screen
        std::cout << ansi::clear_screen << prompt << line_ << std::flush;
        // Move cursor to correct position
        if (cursor_pos_ < line_.size())
        {
          std::cout << "\033[" << (line_.size() - cursor_pos_) << "D" << std::flush;
        }
        continue;
      }

      if (c == Terminal::KEY_BACKSPACE || c == 127 || c == 8)
      {
        backspace();
        show_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_DELETE)
      {
        delete_char();
        show_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_LEFT || c == Terminal::KEY_CTRL_B)
      {
        move_left();
        current_suggestion_.clear();
        refresh_line_with_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_RIGHT || c == Terminal::KEY_CTRL_F)
      {
        move_right();
        show_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_UP || c == Terminal::KEY_CTRL_P)
      {
        history_prev();
        show_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_DOWN || c == Terminal::KEY_CTRL_N)
      {
        history_next();
        show_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_HOME || c == Terminal::KEY_CTRL_A)
      {
        move_home();
        current_suggestion_.clear();
        refresh_line_with_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_END || c == Terminal::KEY_CTRL_E)
      {
        move_end();
        show_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_CTRL_U)
      {
        // Clear line before cursor
        line_.erase(0, cursor_pos_);
        cursor_pos_ = 0;
        show_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_CTRL_K)
      {
        // Clear line after cursor
        line_.erase(cursor_pos_);
        show_suggestion(prompt);
        continue;
      }

      if (c == Terminal::KEY_CTRL_W)
      {
        // Delete word before cursor
        delete_word();
        show_suggestion(prompt);
        continue;
      }

      // Tab key - accept suggestion or indent
      if (c == Terminal::KEY_TAB)
      {
        if (!current_suggestion_.empty() && current_suggestion_.size() > line_.size())
        {
          // Accept the suggestion
          line_ = current_suggestion_;
          cursor_pos_ = line_.size();
          current_suggestion_.clear();
          refresh_line_with_suggestion(prompt);
        }
        else
        {
          // Insert spaces for indentation
          insert_char(' ');
          insert_char(' ');
          show_suggestion(prompt);
        }
        continue;
      }

      // Right arrow at end of line can also accept suggestion
      if ((c == Terminal::KEY_RIGHT || c == Terminal::KEY_END) &&
          cursor_pos_ == line_.size() &&
          !current_suggestion_.empty() && current_suggestion_.size() > line_.size())
      {
        line_ = current_suggestion_;
        cursor_pos_ = line_.size();
        current_suggestion_.clear();
        refresh_line_with_suggestion(prompt);
        continue;
      }

      // Regular character
      if (c >= 32 && c < 127)
      {
        insert_char(static_cast<char>(c));
        show_suggestion(prompt);
        continue;
      }
    }
  }

private:
  void show_suggestion(const std::string& prompt)
  {
    // Get suggestion based on current input
    current_suggestion_ = g_suggestions.get_suggestion(line_);
    refresh_line_with_suggestion(prompt);
  }

  void refresh_line_with_suggestion(const std::string& prompt)
  {
    // Clear line and reprint with ghost text
    std::cout << "\r" << ansi::clear_line << prompt << line_;

    // Show ghost text suggestion (dimmed)
    if (use_colors && !current_suggestion_.empty() &&
        current_suggestion_.size() > line_.size() &&
        cursor_pos_ == line_.size())
    {
      std::string ghost = current_suggestion_.substr(line_.size());
      std::cout << ansi::dim << ghost << ansi::reset;
      // Move cursor back to actual position (before ghost text)
      std::cout << "\033[" << ghost.size() << "D";
    }

    // Move cursor to correct position within line
    if (cursor_pos_ < line_.size())
    {
      std::cout << "\033[" << (line_.size() - cursor_pos_) << "D";
    }
    std::cout << std::flush;
  }

  void insert_char(char c)
  {
    line_.insert(cursor_pos_, 1, c);
    cursor_pos_++;

    // Just update, suggestion will be shown by caller
    // Print from cursor position to end
    std::cout << line_.substr(cursor_pos_ - 1);

    // Move cursor back to position
    if (cursor_pos_ < line_.size())
    {
      std::cout << "\033[" << (line_.size() - cursor_pos_) << "D";
    }
    std::cout << std::flush;
  }

  void backspace()
  {
    if (cursor_pos_ > 0)
    {
      line_.erase(cursor_pos_ - 1, 1);
      cursor_pos_--;

      // Move cursor left, print rest of line, clear extra char, move back
      std::cout << "\033[D" << line_.substr(cursor_pos_) << " ";
      std::cout << "\033[" << (line_.size() - cursor_pos_ + 1) << "D";
      std::cout << std::flush;
    }
  }

  void delete_char()
  {
    if (cursor_pos_ < line_.size())
    {
      line_.erase(cursor_pos_, 1);

      // Print rest of line, clear extra char, move back
      std::cout << line_.substr(cursor_pos_) << " ";
      std::cout << "\033[" << (line_.size() - cursor_pos_ + 1) << "D";
      std::cout << std::flush;
    }
  }

  void delete_word()
  {
    if (cursor_pos_ == 0) return;

    std::size_t end = cursor_pos_;
    // Skip trailing spaces
    while (cursor_pos_ > 0 && line_[cursor_pos_ - 1] == ' ')
      cursor_pos_--;
    // Skip word
    while (cursor_pos_ > 0 && line_[cursor_pos_ - 1] != ' ')
      cursor_pos_--;

    line_.erase(cursor_pos_, end - cursor_pos_);
  }

  void move_left()
  {
    if (cursor_pos_ > 0)
    {
      cursor_pos_--;
      std::cout << "\033[D" << std::flush;
    }
  }

  void move_right()
  {
    if (cursor_pos_ < line_.size())
    {
      cursor_pos_++;
      std::cout << "\033[C" << std::flush;
    }
  }

  void move_home()
  {
    if (cursor_pos_ > 0)
    {
      std::cout << "\033[" << cursor_pos_ << "D" << std::flush;
      cursor_pos_ = 0;
    }
  }

  void move_end()
  {
    if (cursor_pos_ < line_.size())
    {
      std::cout << "\033[" << (line_.size() - cursor_pos_) << "C" << std::flush;
      cursor_pos_ = line_.size();
    }
  }

  void history_prev()
  {
    if (history_.empty()) return;

    if (history_index_ == history_.size())
    {
      saved_line_ = line_;
    }

    if (history_index_ > 0)
    {
      history_index_--;
      line_ = history_[history_index_];
      cursor_pos_ = line_.size();
    }
  }

  void history_next()
  {
    if (history_index_ < history_.size())
    {
      history_index_++;
      if (history_index_ == history_.size())
      {
        line_ = saved_line_;
      }
      else
      {
        line_ = history_[history_index_];
      }
      cursor_pos_ = line_.size();
    }
  }

  void refresh_line(const std::string& prompt)
  {
    // Clear line and reprint
    std::cout << "\r" << ansi::clear_line << prompt << line_;

    // Move cursor to correct position
    if (cursor_pos_ < line_.size())
    {
      std::cout << "\033[" << (line_.size() - cursor_pos_) << "D";
    }
    std::cout << std::flush;
  }

  Terminal& term_;
  std::vector<std::string>& history_;
  std::string line_;
  std::string saved_line_;
  std::string current_suggestion_;
  std::size_t cursor_pos_;
  std::size_t history_index_;
};

// Format duration for display
std::string format_duration(std::chrono::nanoseconds ns)
{
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(ns);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(ns);
  auto s = std::chrono::duration_cast<std::chrono::seconds>(ns);

  std::ostringstream oss;
  if (s.count() > 0)
  {
    oss << std::fixed << std::setprecision(3) << (ms.count() / 1000.0) << "s";
  }
  else if (ms.count() > 0)
  {
    oss << std::fixed << std::setprecision(3) << (us.count() / 1000.0) << "ms";
  }
  else
  {
    oss << us.count() << "us";
  }
  return oss.str();
}

// Get value type name
std::string get_type_name(const neamc::vm::Value& value)
{
  if (value.is_nil()) return "nil";
  if (value.is_bool()) return "bool";
  if (value.is_number()) return "number";
  if (value.is_string()) return "string";
  if (value.is_list()) return "list";
  if (value.is_map()) return "map";
  if (value.is_function()) return "function";
  if (value.is_agent()) return "agent";
  if (value.is_skill()) return "skill";
  if (value.is_knowledge()) return "knowledge";
  if (value.is_option()) return "option";
  if (value.is_future()) return "future";
  return "object";
}

// Print a value in a human-readable format
void print_value(const neamc::vm::Value& value, std::ostream& out, int depth = 0)
{
  if (value.is_nil())
  {
    out << colorize(ansi::dim, "nil");
  }
  else if (value.is_bool())
  {
    out << colorize(ansi::yellow, value.as_bool() ? "true" : "false");
  }
  else if (value.is_number())
  {
    double num = value.as_number();
    if (num == static_cast<int64_t>(num))
    {
      out << colorize(ansi::cyan, std::to_string(static_cast<int64_t>(num)));
    }
    else
    {
      out << colorize(ansi::cyan, std::to_string(num));
    }
  }
  else if (value.is_string())
  {
    auto* str = neamc::vm::as_string(value);
    std::string content(str->chars, str->length);
    out << colorize(ansi::green, "\"" + content + "\"");
  }
  else if (value.is_list())
  {
    auto* list = neamc::vm::as_list(value);
    out << "[";
    for (std::size_t i = 0; i < list->items.size(); ++i)
    {
      if (i > 0) out << ", ";
      print_value(list->items[i], out, depth + 1);
    }
    out << "]";
  }
  else if (value.is_map())
  {
    auto* map = neamc::vm::as_map(value);
    out << "{";
    std::size_t count = 0;
    for (const auto& [key, val] : map->entries)
    {
      if (count > 0) out << ", ";
      out << colorize(ansi::blue, key) << ": ";
      print_value(val, out, depth + 1);
      ++count;
    }
    out << "}";
  }
  else if (value.is_function())
  {
    auto* fn = neamc::vm::as_function(value);
    std::string name = fn->name ? std::string(fn->name->chars, fn->name->length) : "<anonymous>";
    out << colorize(ansi::magenta, "<function " + name + ">");
  }
  else if (value.is_agent())
  {
    auto* agent = neamc::vm::as_agent(value);
    std::string name = agent->name ? std::string(agent->name->chars, agent->name->length) : "unnamed";
    out << colorize(ansi::magenta, "<agent " + name + ">");
  }
  else if (value.is_skill())
  {
    auto* skill = neamc::vm::as_skill(value);
    std::string name = skill->name ? std::string(skill->name->chars, skill->name->length) : "unnamed";
    out << colorize(ansi::magenta, "<skill " + name + ">");
  }
  else if (value.is_knowledge())
  {
    auto* kb = neamc::vm::as_knowledge(value);
    std::string name = kb->name ? std::string(kb->name->chars, kb->name->length) : "unnamed";
    out << colorize(ansi::magenta, "<knowledge " + name + ">");
  }
  else if (value.is_option())
  {
    auto* opt = neamc::vm::as_option(value);
    if (opt->has_value)
    {
      out << "Some(";
      print_value(opt->value, out, depth + 1);
      out << ")";
    }
    else
    {
      out << colorize(ansi::dim, "None");
    }
  }
  else if (value.is_future())
  {
    out << colorize(ansi::magenta, "<future>");
  }
  else
  {
    out << colorize(ansi::dim, "<object>");
  }
}

// Check if input has balanced delimiters
bool is_complete_input(const std::string& input)
{
  int braces = 0, parens = 0, brackets = 0;
  bool in_string = false;
  char prev = 0;

  for (char c : input)
  {
    if (in_string)
    {
      if (c == '"' && prev != '\\') in_string = false;
    }
    else
    {
      switch (c)
      {
        case '"': in_string = true; break;
        case '{': braces++; break;
        case '}': braces--; break;
        case '(': parens++; break;
        case ')': parens--; break;
        case '[': brackets++; break;
        case ']': brackets--; break;
      }
    }
    prev = c;
  }

  return braces <= 0 && parens <= 0 && brackets <= 0 && !in_string;
}

// Trim whitespace
std::string trim(const std::string& str)
{
  const auto start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  const auto end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// Read file contents
std::string read_file(const std::string& path)
{
  std::ifstream file(path);
  if (!file) throw std::runtime_error("Cannot open file: " + path);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Transform for REPL: emit last expression
void transform_for_repl(neamc::Program& program)
{
  if (program.statements.empty()) return;

  auto& last_stmt = program.statements.back();
  if (std::holds_alternative<neamc::ExpressionStmt>(last_stmt->node))
  {
    auto& expr_stmt = std::get<neamc::ExpressionStmt>(last_stmt->node);
    auto expr_ptr = std::move(expr_stmt.expression);

    auto new_stmt = std::make_unique<neamc::Statement>();
    new_stmt->span = last_stmt->span;
    neamc::EmitStmt emit_stmt;
    emit_stmt.value = std::move(expr_ptr);
    new_stmt->node = std::move(emit_stmt);

    program.statements.back() = std::move(new_stmt);
  }
}

// Print help with all commands
void print_help()
{
  std::cout << colorize(ansi::bold, "REPL Commands:\n\n");

  std::cout << colorize(ansi::underline, "General:\n");
  std::cout << "  " << colorize(ansi::cyan, ":help") << ", " << colorize(ansi::cyan, ":h")
            << "              Show this help message\n";
  std::cout << "  " << colorize(ansi::cyan, ":quit") << ", " << colorize(ansi::cyan, ":q")
            << ", " << colorize(ansi::cyan, ":exit")
            << "      Exit the REPL\n";
  std::cout << "  " << colorize(ansi::cyan, ":version") << ", " << colorize(ansi::cyan, ":v")
            << "           Show version\n";
  std::cout << "\n";

  std::cout << colorize(ansi::underline, "File Operations:\n");
  std::cout << "  " << colorize(ansi::cyan, ":load <file>") << ", " << colorize(ansi::cyan, ":l")
            << "      Load and execute a .neam file\n";
  std::cout << "  " << colorize(ansi::cyan, ":reload") << ", " << colorize(ansi::cyan, ":r")
            << "            Reload the last loaded file\n";
  std::cout << "  " << colorize(ansi::cyan, ":save <file>") << ", " << colorize(ansi::cyan, ":s")
            << "      Save history to file\n";
  std::cout << "\n";

  std::cout << colorize(ansi::underline, "Environment:\n");
  std::cout << "  " << colorize(ansi::cyan, ":clear") << ", " << colorize(ansi::cyan, ":c")
            << "             Clear the environment (reset VM)\n";
  std::cout << "  " << colorize(ansi::cyan, ":cls")
            << "                 Clear the screen\n";
  std::cout << "  " << colorize(ansi::cyan, ":vars")
            << "                List defined variables\n";
  std::cout << "\n";

  std::cout << colorize(ansi::underline, "History:\n");
  std::cout << "  " << colorize(ansi::cyan, ":history") << ", " << colorize(ansi::cyan, ":hist")
            << "        Show command history\n";
  std::cout << "  " << colorize(ansi::cyan, ":!<n>")
            << "                Re-execute command #n from history\n";
  std::cout << "  " << colorize(ansi::cyan, ":!!")
            << "                 Re-execute the last command\n";
  std::cout << "\n";

  std::cout << colorize(ansi::underline, "Debugging:\n");
  std::cout << "  " << colorize(ansi::cyan, ":type <expr>") << ", " << colorize(ansi::cyan, ":t")
            << "      Show type of expression\n";
  std::cout << "  " << colorize(ansi::cyan, ":time <expr>")
            << "        Time expression execution\n";
  std::cout << "\n";

  std::cout << colorize(ansi::underline, "Special Variables:\n");
  std::cout << "  " << colorize(ansi::cyan, "_")
            << "                   Last result\n";
  std::cout << "  " << colorize(ansi::cyan, "__")
            << "                  Second-to-last result\n";
  std::cout << "\n";

  std::cout << colorize(ansi::bold, "Keyboard Shortcuts:\n");
  std::cout << "  " << colorize(ansi::cyan, "Tab")
            << "                  Accept autocomplete suggestion\n";
  std::cout << "  " << colorize(ansi::cyan, "Right (at end)")
            << "       Accept autocomplete suggestion\n";
  std::cout << "  " << colorize(ansi::cyan, "Up/Down")
            << "              Navigate command history\n";
  std::cout << "  " << colorize(ansi::cyan, "Left/Right")
            << "           Move cursor\n";
  std::cout << "  " << colorize(ansi::cyan, "Ctrl+A / Home")
            << "        Move to beginning of line\n";
  std::cout << "  " << colorize(ansi::cyan, "Ctrl+E / End")
            << "         Move to end of line\n";
  std::cout << "  " << colorize(ansi::cyan, "Ctrl+U")
            << "               Clear line before cursor\n";
  std::cout << "  " << colorize(ansi::cyan, "Ctrl+K")
            << "               Clear line after cursor\n";
  std::cout << "  " << colorize(ansi::cyan, "Ctrl+W")
            << "               Delete word before cursor\n";
  std::cout << "  " << colorize(ansi::cyan, "Ctrl+L")
            << "               Clear the screen\n";
  std::cout << "  " << colorize(ansi::cyan, "Ctrl+C")
            << "               Cancel current input\n";
  std::cout << "  " << colorize(ansi::cyan, "Ctrl+D")
            << "               Exit (on empty line)\n";
  std::cout << "\n";

  std::cout << colorize(ansi::bold, "Autocomplete:\n");
  std::cout << "  Ghost text suggestions appear as you type (dimmed text)\n";
  std::cout << "  Supports: commands (:), keywords, built-ins, and your variables\n";
  std::cout << "\n";

  std::cout << colorize(ansi::bold, "Examples:\n");
  std::cout << "  " << colorize(ansi::dim, "neam>") << " let x = 42;\n";
  std::cout << "  " << colorize(ansi::dim, "neam>") << " x * 2;\n";
  std::cout << "  " << colorize(ansi::cyan, "84") << "\n";
  std::cout << "  " << colorize(ansi::dim, "neam>") << " _;\n";
  std::cout << "  " << colorize(ansi::cyan, "84") << "\n";
}

void print_banner()
{
  std::cout << colorize(ansi::bold, "Neam") << " " << colorize(ansi::cyan, kVersion);
  std::cout << " - Interactive REPL\n";
  std::cout << "Type " << colorize(ansi::cyan, ":help") << " for commands, ";
  std::cout << colorize(ansi::cyan, ":quit") << " to exit\n\n";
}

// REPL class
class Repl
{
public:
  Repl() : vm_(std::make_unique<neamc::vm::VirtualMachine>()) {}

  void run()
  {
    print_banner();

    Terminal term;
    bool raw_mode_enabled = false;

    if (is_tty())
    {
      raw_mode_enabled = term.enable_raw_mode();
    }

    LineEditor editor(term, history_);

    std::string buffer;

    while (true)
    {
      std::string prompt;
      if (buffer.empty())
      {
        prompt = use_colors ? (std::string(ansi::bold) + "neam> " + ansi::reset) : "neam> ";
      }
      else
      {
        prompt = use_colors ? (std::string(ansi::dim) + "...   " + ansi::reset) : "...   ";
      }

      auto line_opt = editor.read_line(prompt);

      if (!line_opt.has_value())
      {
        // EOF (Ctrl+D)
        break;
      }

      std::string line = line_opt.value();

      // Handle empty line
      if (trim(line).empty() && buffer.empty())
      {
        continue;
      }

      // Handle special commands (only when buffer is empty)
      if (buffer.empty() && !line.empty() && line[0] == ':')
      {
        add_to_history(line);
        if (!handle_command(line))
        {
          break;  // :quit command
        }
        continue;
      }

      // Add first line to history
      if (buffer.empty() && !trim(line).empty())
      {
        add_to_history(line);
      }

      // Accumulate input
      buffer += line + "\n";

      // Check if input is complete
      if (!is_complete_input(buffer))
      {
        continue;
      }

      // Execute the input
      execute(buffer, true);
      buffer.clear();
    }

    term.disable_raw_mode();
    std::cout << colorize(ansi::dim, "Goodbye!") << "\n";
  }

  bool execute(const std::string& source, bool store_result = false)
  {
    try
    {
      neamc::Parser parser(source);
      auto program = parser.parse();
      transform_for_repl(program);

      neamc::Compiler compiler;
      auto chunk = compiler.compile(program);

      std::size_t prev_emitted = vm_->emitted().size();
      vm_->run(chunk);

      const auto& emitted = vm_->emitted();
      for (std::size_t i = prev_emitted; i < emitted.size(); ++i)
      {
        const auto& value = emitted[i];
        if (!value.is_nil())
        {
          print_value(value, std::cout);
          std::cout << "\n";

          if (store_result)
          {
            store_last_result(value);
          }
        }
      }

      // Track user-defined symbols for autocomplete
      extract_user_symbols(source);

      return true;
    }
    catch (const std::exception& e)
    {
      std::cerr << colorize(ansi::red, "Error: ") << e.what() << "\n";
      return false;
    }
  }

  // Extract variable and function names from source for autocomplete
  void extract_user_symbols(const std::string& source)
  {
    // Find "let <name> =" patterns
    std::size_t pos = 0;
    while ((pos = source.find("let ", pos)) != std::string::npos)
    {
      pos += 4; // Skip "let "
      // Skip whitespace
      while (pos < source.size() && std::isspace(source[pos])) pos++;

      // Extract identifier
      std::string name;
      while (pos < source.size() && (std::isalnum(source[pos]) || source[pos] == '_'))
      {
        name += source[pos++];
      }

      if (!name.empty())
      {
        g_suggestions.add_user_symbol(name);
      }
    }

    // Find "fun <name>(" patterns (named functions)
    pos = 0;
    while ((pos = source.find("fun ", pos)) != std::string::npos)
    {
      pos += 4; // Skip "fun "
      // Skip whitespace
      while (pos < source.size() && std::isspace(source[pos])) pos++;

      // Check if it's a named function (not anonymous)
      if (pos < source.size() && source[pos] != '(')
      {
        std::string name;
        while (pos < source.size() && (std::isalnum(source[pos]) || source[pos] == '_'))
        {
          name += source[pos++];
        }
        if (!name.empty())
        {
          g_suggestions.add_user_symbol(name + "(");
        }
      }
    }

    // Find "agent <name>" patterns
    pos = 0;
    while ((pos = source.find("agent ", pos)) != std::string::npos)
    {
      pos += 6; // Skip "agent "
      while (pos < source.size() && std::isspace(source[pos])) pos++;

      std::string name;
      while (pos < source.size() && (std::isalnum(source[pos]) || source[pos] == '_'))
      {
        name += source[pos++];
      }
      if (!name.empty())
      {
        g_suggestions.add_user_symbol(name);
      }
    }

    // Find "skill <name>" patterns
    pos = 0;
    while ((pos = source.find("skill ", pos)) != std::string::npos)
    {
      pos += 6; // Skip "skill "
      while (pos < source.size() && std::isspace(source[pos])) pos++;

      std::string name;
      while (pos < source.size() && (std::isalnum(source[pos]) || source[pos] == '_'))
      {
        name += source[pos++];
      }
      if (!name.empty())
      {
        g_suggestions.add_user_symbol(name);
      }
    }
  }

  bool execute_timed(const std::string& source)
  {
    try
    {
      neamc::Parser parser(source);
      auto program = parser.parse();
      transform_for_repl(program);

      neamc::Compiler compiler;
      auto chunk = compiler.compile(program);

      std::size_t prev_emitted = vm_->emitted().size();

      auto start = std::chrono::high_resolution_clock::now();
      vm_->run(chunk);
      auto end = std::chrono::high_resolution_clock::now();

      auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

      const auto& emitted = vm_->emitted();
      for (std::size_t i = prev_emitted; i < emitted.size(); ++i)
      {
        const auto& value = emitted[i];
        if (!value.is_nil())
        {
          print_value(value, std::cout);
          std::cout << "\n";
          store_last_result(value);
        }
      }

      std::cout << colorize(ansi::dim, "Time: ")
                << colorize(ansi::cyan, format_duration(duration)) << "\n";

      return true;
    }
    catch (const std::exception& e)
    {
      std::cerr << colorize(ansi::red, "Error: ") << e.what() << "\n";
      return false;
    }
  }

  bool execute_type(const std::string& source)
  {
    try
    {
      neamc::Parser parser(source);
      auto program = parser.parse();
      transform_for_repl(program);

      neamc::Compiler compiler;
      auto chunk = compiler.compile(program);

      std::size_t prev_emitted = vm_->emitted().size();
      vm_->run(chunk);

      const auto& emitted = vm_->emitted();
      for (std::size_t i = prev_emitted; i < emitted.size(); ++i)
      {
        const auto& value = emitted[i];
        std::cout << colorize(ansi::dim, "type: ")
                  << colorize(ansi::cyan, get_type_name(value)) << "\n";
        std::cout << colorize(ansi::dim, "value: ");
        print_value(value, std::cout);
        std::cout << "\n";
      }

      return true;
    }
    catch (const std::exception& e)
    {
      std::cerr << colorize(ansi::red, "Error: ") << e.what() << "\n";
      return false;
    }
  }

  bool load_file(const std::string& path)
  {
    try
    {
      std::string source = read_file(path);
      std::cout << colorize(ansi::dim, "Loading ") << path << "...\n";

      neamc::Parser parser(source);
      auto program = parser.parse();

      neamc::Compiler compiler;
      auto chunk = compiler.compile(program);

      std::size_t prev_emitted = vm_->emitted().size();
      vm_->run(chunk);

      const auto& emitted = vm_->emitted();
      for (std::size_t i = prev_emitted; i < emitted.size(); ++i)
      {
        print_value(emitted[i], std::cout);
        std::cout << "\n";
      }

      std::cout << colorize(ansi::green, "Loaded ") << path << "\n";
      last_loaded_file_ = path;
      return true;
    }
    catch (const std::exception& e)
    {
      std::cerr << colorize(ansi::red, "Error loading file: ") << e.what() << "\n";
      return false;
    }
  }

  void reset()
  {
    vm_ = std::make_unique<neamc::vm::VirtualMachine>();
    g_suggestions.clear_user_symbols();
    std::cout << colorize(ansi::dim, "Environment cleared.") << "\n";
  }

  void clear_screen()
  {
    std::cout << ansi::clear_screen << std::flush;
  }

  void show_vars()
  {
    std::cout << colorize(ansi::bold, "Special variables:\n");
    std::cout << "  " << colorize(ansi::cyan, "_") << "  = ";
    print_value(last_result_, std::cout);
    std::cout << "\n";
    std::cout << "  " << colorize(ansi::cyan, "__") << " = ";
    print_value(second_last_result_, std::cout);
    std::cout << "\n";
    std::cout << "\n";
    std::cout << colorize(ansi::dim, "(Use expressions to inspect globals, e.g., 'myVar;')\n");
  }

  void show_history()
  {
    std::cout << colorize(ansi::bold, "Command History:\n");
    for (std::size_t i = 0; i < history_.size(); ++i)
    {
      std::cout << "  " << colorize(ansi::dim, std::to_string(i + 1) + ": ")
                << history_[i] << "\n";
    }
    if (history_.empty())
    {
      std::cout << colorize(ansi::dim, "  (no history yet)\n");
    }
  }

  bool save_history(const std::string& path)
  {
    try
    {
      std::ofstream file(path);
      if (!file)
      {
        throw std::runtime_error("Cannot open file for writing: " + path);
      }
      for (const auto& cmd : history_)
      {
        file << cmd << "\n";
      }
      std::cout << colorize(ansi::green, "Saved ") << history_.size()
                << " commands to " << path << "\n";
      return true;
    }
    catch (const std::exception& e)
    {
      std::cerr << colorize(ansi::red, "Error saving history: ") << e.what() << "\n";
      return false;
    }
  }

private:
  void add_to_history(const std::string& cmd)
  {
    std::string trimmed = trim(cmd);
    if (trimmed.empty()) return;

    // Don't add duplicate consecutive entries
    if (!history_.empty() && history_.back() == trimmed) return;

    if (history_.size() >= kMaxHistorySize)
    {
      history_.erase(history_.begin());
    }
    history_.push_back(trimmed);
  }

  void store_last_result(const neamc::vm::Value& value)
  {
    second_last_result_ = last_result_;
    last_result_ = value;

    try
    {
      std::string setup_code;
      if (value.is_number())
      {
        setup_code = "let _ = " + std::to_string(value.as_number()) + ";";
      }
      else if (value.is_bool())
      {
        setup_code = std::string("let _ = ") + (value.as_bool() ? "true" : "false") + ";";
      }
      else if (value.is_string())
      {
        auto* str = neamc::vm::as_string(value);
        std::string content(str->chars, str->length);
        std::string escaped;
        for (char c : content)
        {
          if (c == '"') escaped += "\\\"";
          else if (c == '\\') escaped += "\\\\";
          else escaped += c;
        }
        setup_code = "let _ = \"" + escaped + "\";";
      }
      else if (value.is_nil())
      {
        setup_code = "let _ = nil;";
      }

      if (!setup_code.empty())
      {
        neamc::Parser parser(setup_code);
        auto program = parser.parse();
        neamc::Compiler compiler;
        auto chunk = compiler.compile(program);
        vm_->run(chunk);
      }
    }
    catch (...)
    {
    }
  }

  bool handle_command(const std::string& cmd)
  {
    std::string trimmed = trim(cmd);

    if (trimmed == ":quit" || trimmed == ":q" || trimmed == ":exit")
    {
      return false;
    }

    if (trimmed == ":help" || trimmed == ":h" || trimmed == ":?")
    {
      print_help();
      return true;
    }

    if (trimmed == ":version" || trimmed == ":v")
    {
      std::cout << "neam-cli " << kVersion << "\n";
      return true;
    }

    if (trimmed == ":clear" || trimmed == ":c")
    {
      reset();
      return true;
    }

    if (trimmed == ":cls")
    {
      clear_screen();
      return true;
    }

    if (trimmed == ":vars")
    {
      show_vars();
      return true;
    }

    if (trimmed == ":history" || trimmed == ":hist")
    {
      show_history();
      return true;
    }

    if (trimmed == ":!!")
    {
      if (history_.size() < 2)
      {
        std::cerr << colorize(ansi::red, "No previous command\n");
        return true;
      }
      // Get second-to-last since we just added :!! to history
      std::string last_cmd = history_[history_.size() - 2];
      std::cout << colorize(ansi::dim, ">> ") << last_cmd << "\n";
      if (last_cmd[0] != ':')
      {
        execute(last_cmd + "\n", true);
      }
      return true;
    }

    if (trimmed.rfind(":!", 0) == 0 && trimmed.size() > 2)
    {
      try
      {
        int n = std::stoi(trimmed.substr(2));
        if (n < 1 || static_cast<std::size_t>(n) > history_.size() - 1)
        {
          std::cerr << colorize(ansi::red, "Invalid history number\n");
          return true;
        }
        std::string cmd_to_run = history_[n - 1];
        std::cout << colorize(ansi::dim, ">> ") << cmd_to_run << "\n";
        if (cmd_to_run[0] != ':')
        {
          execute(cmd_to_run + "\n", true);
        }
      }
      catch (...)
      {
        std::cerr << colorize(ansi::red, "Invalid history number\n");
      }
      return true;
    }

    if (trimmed == ":reload" || trimmed == ":r")
    {
      if (last_loaded_file_.empty())
      {
        std::cerr << colorize(ansi::red, "No file has been loaded yet\n");
        return true;
      }
      load_file(last_loaded_file_);
      return true;
    }

    if (trimmed.rfind(":load ", 0) == 0 || trimmed.rfind(":l ", 0) == 0)
    {
      std::string path;
      if (trimmed.rfind(":load ", 0) == 0)
        path = trim(trimmed.substr(6));
      else
        path = trim(trimmed.substr(3));
      load_file(path);
      return true;
    }

    if (trimmed.rfind(":save ", 0) == 0 || trimmed.rfind(":s ", 0) == 0)
    {
      std::string path;
      if (trimmed.rfind(":save ", 0) == 0)
        path = trim(trimmed.substr(6));
      else
        path = trim(trimmed.substr(3));
      save_history(path);
      return true;
    }

    if (trimmed.rfind(":type ", 0) == 0 || trimmed.rfind(":t ", 0) == 0)
    {
      std::string expr;
      if (trimmed.rfind(":type ", 0) == 0)
        expr = trim(trimmed.substr(6));
      else
        expr = trim(trimmed.substr(3));

      if (!expr.empty() && expr.back() != ';')
        expr += ";";

      execute_type(expr);
      return true;
    }

    if (trimmed.rfind(":time ", 0) == 0)
    {
      std::string expr = trim(trimmed.substr(6));

      if (!expr.empty() && expr.back() != ';')
        expr += ";";

      execute_timed(expr);
      return true;
    }

    std::cerr << colorize(ansi::red, "Unknown command: ") << trimmed << "\n";
    std::cerr << "Type " << colorize(ansi::cyan, ":help") << " for available commands\n";
    return true;
  }

  std::unique_ptr<neamc::vm::VirtualMachine> vm_;
  std::vector<std::string> history_;
  std::string last_loaded_file_;
  neamc::vm::Value last_result_{neamc::vm::Value::Nil()};
  neamc::vm::Value second_last_result_{neamc::vm::Value::Nil()};
};

void print_usage()
{
  std::cout << "Usage:\n";
  std::cout << "  neam-cli              Start interactive REPL\n";
  std::cout << "  neam-cli <file.neam>  Execute file then enter REPL\n";
  std::cout << "  neam-cli -e \"code\"    Execute code and exit\n";
  std::cout << "  neam-cli --help       Show this help\n";
  std::cout << "  neam-cli --version    Show version\n";
}

}  // namespace

int main(int argc, char** argv)
{
  use_colors = is_tty();

  if (argc == 1)
  {
    Repl repl;
    repl.run();
    return 0;
  }

  std::string arg1 = argv[1];

  if (arg1 == "--help" || arg1 == "-h")
  {
    print_usage();
    return 0;
  }

  if (arg1 == "--version" || arg1 == "-v")
  {
    std::cout << "neam-cli " << kVersion << "\n";
    return 0;
  }

  if (arg1 == "-e" && argc >= 3)
  {
    use_colors = false;
    Repl repl;
    return repl.execute(argv[2]) ? 0 : 1;
  }

  Repl repl;
  if (!repl.load_file(arg1))
  {
    return 1;
  }
  std::cout << "\n";
  repl.run();
  return 0;
}
