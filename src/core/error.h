// Error types shared by the whole program.
#pragma once

#include <stdexcept>
#include <string>

namespace sh {

// Thrown when an original game file is missing, truncated or malformed.
class DataError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

}  // namespace sh
