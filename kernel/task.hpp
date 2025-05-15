#pragma once

#include <array>
#include <cstdint>

struct TaskContext {
  uint64_t cr3, rip, rflags, reserved1;
  uint64_t cs, ss, fs, gs;
  uint64_t rax, rbx, rcx, rdx, rdi, rsi, rsp, rbp;
  uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
  std::array<uint8_t, 512> fxsaved_area;
} __attribute__((packed));

extern TaskContext task_b_ctx, task_a_ctx;

void SwitchTask();
void InitializeTask();