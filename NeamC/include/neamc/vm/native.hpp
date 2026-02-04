// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Native function interface
//

#pragma once

#include <string>

#include "neamc/vm/object.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
class VirtualMachine;

void register_core_natives(VirtualMachine& vm);
}  // namespace neamc::vm
