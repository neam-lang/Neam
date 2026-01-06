//
// Neam Virtual Machine - Bytecode implementation
//

#include "neamc/vm/bytecode.hpp"

#include <array>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace
{
constexpr std::array<uint8_t, 4> kMagic{{'N', 'E', 'A', 'M'}};
constexpr uint8_t kVersion = 0x01;
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
}

void Bytecode::write_short(uint16_t value)
{
  const uint8_t low = static_cast<uint8_t>(value & 0xFF);
  const uint8_t high = static_cast<uint8_t>((value >> 8) & 0xFF);
  code_.push_back(low);
  code_.push_back(high);
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
  switch (value.type())
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
    case ValueType::String:
      type = 3;
      break;
    case ValueType::Agent:
      type = 4;
      break;
  }
  out.put(static_cast<char>(type));
  if (!out)
  {
    throw std::runtime_error("Failed to write value type");
  }

  switch (value.type())
  {
    case ValueType::Nil:
      break;
    case ValueType::Bool:
      out.put(static_cast<char>(value.as_bool() ? 1 : 0));
      break;
    case ValueType::Number:
    {
      double number = value.as_number();
      out.write(reinterpret_cast<const char*>(&number), sizeof(number));
      break;
    }
    case ValueType::String:
      write_string(out, value.as_string());
      break;
    case ValueType::Agent:
      write_string(out, value.as_agent().name);
      break;
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
      return Value::String(read_string(in));
    case 4:
      return Value::Agent(read_string(in));
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
  if (static_cast<uint8_t>(version_char) != kVersion)
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

  return chunk;
}
}  // namespace neamc::vm
