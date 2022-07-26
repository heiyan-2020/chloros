#include <chloros.h>
#include <atomic>
#include <common.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <unordered_set>
#include <vector>
#include <iostream>

std::atomic<int> counter{0};

void Worker(void*) {
  ++counter;
  ASSERT(chloros::Yield(false) == true);
  ++counter;
  ASSERT(chloros::Yield(true) == false);
  chloros::Yield();
}

void WorkerWithArgument(void* arg) { counter = *reinterpret_cast<int*>(&arg); }

static void CheckYield() {
  ASSERT(chloros::Yield() == false);
  chloros::Spawn(Worker, nullptr);
  ASSERT(counter == 1);
  chloros::Wait();
  std::cout << "hello" << std::endl;
  ASSERT(counter == 2);
  std::cout << "hi" << std::endl;
  ASSERT(chloros::Yield() == false);
}

static void CheckSpawn() {
  chloros::Spawn(WorkerWithArgument, reinterpret_cast<void*>(42));
  chloros::Wait();
  ASSERT(counter == 42);
}

void WorkerWithArithmetic(void*) {
  float a = 42;
  counter = sqrt(a);
}

static void CheckStackAlignment() {
  chloros::Spawn(WorkerWithArithmetic, nullptr);
  ASSERT(counter == 6);
}

constexpr int const kLoopTimes = 100;
std::vector<int> loop_values{};

void WorkerYieldLoop(void*) {
  for (int i = 0; i < kLoopTimes; ++i) {
    loop_values.push_back(i);
    chloros::Yield();
  }
}

static void CheckYieldLoop() {
  chloros::Spawn(WorkerYieldLoop, nullptr);
  chloros::Spawn(WorkerYieldLoop, nullptr);
  chloros::Wait();
  ASSERT(loop_values.size() == kLoopTimes * 2);
  for (int i = 0; i < kLoopTimes; ++i) {
    ASSERT(loop_values.at(i * 2) == i);
    ASSERT(loop_values.at(i * 2 + 1) == i);
  }
}

int main() {
  chloros::Initialize();
  CheckYield();
  std::cout << "CheckYield success" << std::endl;
  CheckSpawn();
  std::cout << "CheckSpawn success" << std::endl;
  CheckStackAlignment();
  std::cout << "CheckAlignment success" << std::endl;
  CheckYieldLoop();
  std::cout << "CheckYieldLoop success" << std::endl;
  LOG("Phase 3 passed!");
  return 0;
}
