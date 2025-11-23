#include <cstring>
#include <cstdlib>
// #include "../../kernel/graphics.hpp"
// #include "../../kernel/font.hpp"

// auto& printk = *reinterpret_cast<int (*)(const char*, ...)>(0x00110a20);
// auto& fill_rect = *reinterpret_cast<decltype(FillRectangle)*>(0x00000000014c520);
// auto& write_str = *reinterpret_cast<decltype(WriteString)*>(0x000000000014c350);
// auto& scrn_writer = *reinterpret_cast<decltype(screen_writer)*>(0x0000000002870c8);

int stack_ptr;
long stack[100];

long Pop() {
  long value = stack[stack_ptr];
  --stack_ptr;
  return value;
}

void Push(long value) {
  ++stack_ptr;
  stack[stack_ptr] = value;
}

extern "C" int main(int argc, char** argv) {
  stack_ptr = -1;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "+") == 0) {
      long b = Pop();
      long a = Pop();
      Push(a + b);
      // printk("[%d] <- %ld\n", stack_ptr, a + b);
    } else if (strcmp(argv[i], "-") == 0) {
      long b = Pop();
      long a = Pop();
      Push(a - b);
      // printk("[%d] <- %ld\n", stack_ptr, a - b);
    } else {
      long a = atol(argv[i]);
      Push(a);
      // printk("[%d] <- %ld\n", stack_ptr, a);
    }
  }

  // fill_rect(*scrn_writer, Vector2D<int>{100, 10}, Vector2D<int>{200, 200}, ToColor(0x66ff66));
  // write_str(*scrn_writer, Vector2D<int>{100 + 50, 10 + 50}, "Hello RPN World!", ToColor(0xff66ff));

  if (stack_ptr < 0) {
    return 0;
  }

  while (1);
  // return static_cast<int>(Pop());
}
