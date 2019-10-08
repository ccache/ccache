#pragma once

#include <string>
#include <string_view>

namespace llvm { namespace yaml {

std::string escape(std::string_view Input);

}
}
