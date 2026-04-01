//
// Neam v1.1 — NeamOS Foundation Runtime Types
// Factory functions for knowledge cards, personas, governance, adapters, blueprints
//

#include "neamc/vm/neamos_types.hpp"

namespace neamc::vm {

// Factory functions are not needed for v1.1 — all NeamOS types are
// created via the consolidated DIO opcode handler (sub_types 16-19)
// which reuses the existing DIOAgent object with mode tagging.
//
// When dedicated ObjType construction is needed (v1.2+), factory
// functions will be added here following the pattern from
// security_v10_types.cpp and other *_types.cpp files.

} // namespace neamc::vm
