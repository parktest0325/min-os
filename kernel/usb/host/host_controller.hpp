#pragma once
#include "error.hpp"

namespace usb {
  class HostController {
  public:
    virtual ~HostController() = default;
    virtual Error Initialize() = 0;
  };
}
