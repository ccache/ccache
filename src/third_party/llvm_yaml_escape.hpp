#pragma once

#include "nonstd/string_view.hpp"

namespace llvm { namespace yaml {

std::string escape(nonstd::string_view Input);

}
}
