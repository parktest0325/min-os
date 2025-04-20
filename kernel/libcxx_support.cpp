#include <new>
#include <cerrno>

extern "C" int posix_memalign(void**, size_t, size_t) {
  return ENOMEM;
}
