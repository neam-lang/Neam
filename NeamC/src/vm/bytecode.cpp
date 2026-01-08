//
// Neam Virtual Machine - Bytecode implementation
//

#include "neamc/vm/bytecode.hpp"

#include <array>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "neamc/vm/object.hpp"
namespace
{
constexpr std::array<uint8_t, 4> kMagic{{'N', 'E', 'A', 'M'}};
constexpr uint8_t kVersion = 0x02;
}  // namespace

namespace neamc::vm
{
std::size_t Bytecode::add_constant(Value value)
{
  constants_.push_back(std::move(value));
  return constants_.size() - 1;
}

void Bytecode::write_op(OpCode op)
{
  code_.push_back(static_cast<uint8_t>(op));
  if (current_line_ != std::numeric_limits<std::size_t>::max())
  {
    const std::size_t offset = code_.size() - 1;
    if (source_map_.empty() || source_map_.back().offset != offset ||
        source_map_.back().line != current_line_)
    {
      source_map_.push_back(SourceMapEntry{offset, current_line_});
    }
  }
}

void Bytecode::write_byte(uint8_t value)
{
  code_.push_back(value);
}

void Bytecode::write_short(uint16_t value)
{
  const uint8_t low = static_cast<uint8_t>(value & 0xFF);
  const uint8_t high = static_cast<uint8_t>((value >> 8) & 0xFF);
  code_.push_back(low);
  code_.push_back(high);
}

void Bytecode::patch_short(std::size_t index, uint16_t value)
{
  if (index + 1 >= code_.size())
  {
    throw std::runtime_error("Patch index out of range");
  }
  code_[index] = static_cast<uint8_t>(value & 0xFF);
  code_[index + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void Bytecode::emit_constant(Value value)
{
  write_op(OpCode::OP_CONST);
  const auto index = add_constant(std::move(value));
  if (index > std::numeric_limits<uint16_t>::max())
  {
    throw std::overflow_error("Too many constants in bytecode chunk");
  }
  write_short(static_cast<uint16_t>(index));
}

namespace
{
void write_u32(std::ostream& out, uint32_t value)
{
  const uint8_t bytes[4] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF)};
  out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
  if (!out)
  {
    throw std::runtime_error("Failed to write u32");
  }
}

uint32_t read_u32(std::istream& in)
{
  uint8_t bytes[4] = {};
  in.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (in.gcount() != sizeof(bytes))
  {
    throw std::runtime_error("Failed to read u32");
  }
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

void write_string(std::ostream& out, const std::string& value)
{
  write_u32(out, static_cast<uint32_t>(value.size()));
  out.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!out)
  {
    throw std::runtime_error("Failed to write string");
  }
}

std::string read_string(std::istream& in)
{
  const auto size = read_u32(in);
  std::string value(size, '\0');
  in.read(value.data(), static_cast<std::streamsize>(size));
  if (static_cast<uint32_t>(in.gcount()) != size)
  {
    throw std::runtime_error("Failed to read string");
  }
  return value;
}

void write_value(std::ostream& out, const Value& value)
{
  uint8_t type = 0;
  switch (value.type)
  {
    case ValueType::Nil:
      type = 0;
      break;
    case ValueType::Bool:
      type = 1;
      break;
    case ValueType::Number:
      type = 2;
      break;
    case ValueType::Obj:
    {
      if (is_obj_type(value, ObjType::OBJ_STRING))
      {
        type = 3;
      }
      else if (is_obj_type(value, ObjType::OBJ_FUNCTION))
      {
        type = 4;
      }
      else
      {
        type = 5;  // native
      }
      break;
    }
  }
  out.put(static_cast<char>(type));
  if (!out)
  {
    throw std::runtime_error("Failed to write value type");
  }

  switch (type)
  {
    case 0:
      break;
    case 1:
      out.put(static_cast<char>(value.as_bool() ? 1 : 0));
      break;
    case 2:
    {
      double number = value.as_number();
      out.write(reinterpret_cast<const char*>(&number), sizeof(number));
      break;
    }
    case 3:
    {
      const auto* string = as_string(value);
      write_string(out, std::string(string->chars, string->length));
      break;
    }
    case 4:
    {
      const auto* fn = as_function(value);
      const std::string name_str =
          fn->name ? std::string(fn->name->chars, fn->name->length) : std::string();
      write_string(out, name_str);
      write_u32(out, static_cast<uint32_t>(fn->arity));
      std::stringstream nested;
      fn->chunk.serialize(nested);
      const auto nested_str = nested.str();
      write_u32(out, static_cast<uint32_t>(nested_str.size()));
      out.write(nested_str.data(), static_cast<std::streamsize>(nested_str.size()));
      break;
    }
    case 5:
    {
      const auto* native = as_native(value);
      const std::string name_str =
          native->name ? std::string(native->name->chars, native->name->length) : std::string();
      write_string(out, name_str);
      write_u32(out, static_cast<uint32_t>(native->arity));
      break;
    }
  }

  if (!out)
  {
    throw std::runtime_error("Failed to write value payload");
  }
}

Value read_value(std::istream& in)
{
  const auto type_char = in.get();
  if (type_char == std::char_traits<char>::eof())
  {
    throw std::runtime_error("Failed to read value type");
  }
  const uint8_t type = static_cast<uint8_t>(type_char);
  switch (type)
  {
    case 0:
      return Value::Nil();
    case 1:
    {
      const auto b = in.get();
      if (b == std::char_traits<char>::eof())
      {
        throw std::runtime_error("Failed to read bool payload");
      }
      return Value::Bool(b != 0);
    }
    case 2:
    {
      double number = 0.0;
      in.read(reinterpret_cast<char*>(&number), sizeof(number));
      if (in.gcount() != static_cast<std::streamsize>(sizeof(number)))
      {
        throw std::runtime_error("Failed to read number payload");
      }
      return Value::Number(number);
    }
    case 3:
    {
      const auto s = read_string(in);
      return Value::String(s.c_str(), s.size());
    }
    case 4:
    {
      const auto name = read_string(in);
      const auto arity = read_u32(in);
      const auto nested_size = read_u32(in);
      std::string nested_data(nested_size, '\0');
      in.read(nested_data.data(), static_cast<std::streamsize>(nested_size));
      if (static_cast<uint32_t>(in.gcount()) != nested_size)
      {
        throw std::runtime_error("Failed to read nested function chunk");
      }
      std::stringstream nested_stream(nested_data);
      ObjFunction* fn = new_function();
      fn->name = copy_string(name.c_str(), name.size());
      fn->arity = static_cast<int>(arity);
      fn->chunk = Bytecode::deserialize(nested_stream);
      return Value::FunctionValue(fn);
    }
    case 5:
    {
      const auto name = read_string(in);
      const auto arity = read_u32(in);
      auto* native_name = copy_string(name.c_str(), name.size());
      return Value::Native(new_native(native_name, static_cast<int>(arity), nullptr));
    }
    default:
      throw std::runtime_error("Unknown value type tag");
  }
}
}  // namespace

void Bytecode::serialize(std::ostream& out) const
{
  out.write(reinterpret_cast<const char*>(kMagic.data()), static_cast<std::streamsize>(kMagic.size()));
  out.put(static_cast<char>(kVersion));
  if (!out)
  {
    throw std::runtime_error("Failed to write bundle header");
  }

  write_string(out, manifest_);

  write_u32(out, static_cast<uint32_t>(code_.size()));
  if (!code_.empty())
  {
    out.write(reinterpret_cast<const char*>(code_.data()), static_cast<std::streamsize>(code_.size()));
  }
  if (!out)
  {
    throw std::runtime_error("Failed to write code section");
  }

  write_u32(out, static_cast<uint32_t>(constants_.size()));
  for (const auto& constant : constants_)
  {
    write_value(out, constant);
  }

  write_u32(out, static_cast<uint32_t>(source_map_.size()));
  for (const auto& entry : source_map_)
  {
    write_u32(out, static_cast<uint32_t>(entry.offset));
    write_u32(out, static_cast<uint32_t>(entry.line));
  }
}

Bytecode Bytecode::deserialize(std::istream& in)
{
  std::array<uint8_t, 4> magic{};
  in.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
  if (in.gcount() != static_cast<std::streamsize>(magic.size()) || magic != kMagic)
  {
    throw std::runtime_error("Invalid bundle magic");
  }
  const auto version_char = in.get();
  if (version_char == std::char_traits<char>::eof())
  {
    throw std::runtime_error("Missing bundle version");
  }
  const auto version = static_cast<uint8_t>(version_char);
  if (version != 0x01 && version != kVersion)
  {
    throw std::runtime_error("Unsupported bundle version");
  }

  Bytecode chunk;
  chunk.manifest_ = read_string(in);

  const auto code_size = read_u32(in);
  chunk.code_.resize(code_size);
  if (code_size > 0)
  {
    in.read(reinterpret_cast<char*>(chunk.code_.data()), static_cast<std::streamsize>(code_size));
    if (static_cast<uint32_t>(in.gcount()) != code_size)
    {
      throw std::runtime_error("Failed to read code section");
    }
  }

  const auto constant_count = read_u32(in);
  chunk.constants_.reserve(constant_count);
  for (uint32_t i = 0; i < constant_count; ++i)
  {
    chunk.constants_.push_back(read_value(in));
  }

  if (version >= 0x02)
  {
    const auto map_count = read_u32(in);
    chunk.source_map_.reserve(map_count);
    for (uint32_t i = 0; i < map_count; ++i)
    {
      const auto offset = read_u32(in);
      const auto line = read_u32(in);
      chunk.source_map_.push_back(SourceMapEntry{offset, line});
    }
  }

  return chunk;
}
}  // namespace neamc::vm
