#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "error.hpp"
#include "message.hpp"

struct TaskContext {
  uint64_t cr3, rip, rflags, reserved1;
  uint64_t cs, ss, fs, gs;
  uint64_t rax, rbx, rcx, rdx, rdi, rsi, rsp, rbp;
  uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
  std::array<uint8_t, 512> fxsave_area;
} __attribute__((packed));

using TaskFunc = void (uint64_t, int64_t);

class Task {
public:
  static const size_t kDefaultStackBytes = 4096;
  Task(uint64_t id);
  Task& InitContext(TaskFunc* f, int64_t data);
  TaskContext& Context();
  uint64_t ID() const;
  Task& Sleep();
  Task& Wakeup();
  void SendMessage(const Message& msg);
  std::optional<Message> ReceiveMessage();

private:
  uint64_t id_;
  std::vector<uint64_t> stack_;
  alignas(16) TaskContext context_;
  std::deque<Message> msgs_;
};

class TaskManager {
public:
  TaskManager();
  Task& NewTask();
  void SwitchTask(bool current_sleep = false);

  void Sleep(Task* task);
  Error Sleep(uint64_t id);
  void Wakeup(Task* task);
  Error Wakeup(uint64_t id);
  Error SendMessage(uint64_t id, const Message& msg);
  Task& CurrentTask();

private:
  std::vector<std::unique_ptr<Task>> tasks_{};
  uint64_t latest_id_{0};
  std::deque<Task*> running_{};
};

extern TaskManager* task_manager;

void InitializeTask();