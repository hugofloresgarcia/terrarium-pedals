// stack_test.cpp
#include <iostream>
#include <string>
#include <cstdlib>
#include "stack.h"   // <- your header: save it as stack.h in the same folder

// A tiny test helper for readable PASS/FAIL output.
#define CHECK(cond, msg)                                                          \
    do {                                                                          \
        if (cond) {                                                               \
            std::cout << "✔ " << msg << "\n";                                     \
        } else {                                                                  \
            std::cerr << "✘ " << msg << "\n";                                     \
            std::exit(1);                                                         \
        }                                                                         \
    } while (0)

// Dump stack contents from top to bottom without mutating the original.
// We copy the stack by value, then pop from the copy to print.
template <typename T, std::size_t N>
void dump_stack(const stack<T, N>& s, const char* label)
{
    auto copy = s; // copy so we can pop for inspection
    std::cout << "\n--- " << label << " ---\n";
    std::cout << "size=" << copy.size()
              << " capacity=" << copy.capacity()
              << " empty=" << (copy.empty() ? "true" : "false")
              << " full=" << (copy.full() ? "true" : "false")
              << "\ncontents [top -> bottom]: ";

    if (copy.empty()) {
        std::cout << "(empty)\n";
        return;
    }

    std::cout << "[ ";
    bool first = true;
    while (!copy.empty()) {
        T v{};
        bool ok = copy.pop(v);
        if (!ok) break;
        if (!first) std::cout << ", ";
        std::cout << v;
        first = false;
    }
    std::cout << " ]\n";
}

// Test 1: int stack, capacity 3; push to full, fail on overflow, pop to empty.
void test_int_basic()
{
    std::cout << "\n== Test 1: int basic push/peek/pop, capacity=3 ==\n";
    stack<int, 3> s;

    CHECK(s.empty(), "new stack is empty");
    CHECK(!s.full(), "new stack is not full");
    CHECK(s.capacity() == 3, "capacity() == 3");

    // Push 3 items
    CHECK(s.push(10), "push 10");
    CHECK(s.push(20), "push 20");
    CHECK(s.push(30), "push 30 -> reaches full");

    CHECK(s.full(), "stack is full after 3 pushes");
    dump_stack(s, "after 3 pushes");

    // Overflow attempt
    CHECK(!s.push(40), "push fails when full");

    // Peek top
    int top{};
    CHECK(s.peek(top) && top == 30, "peek sees 30");
    dump_stack(s, "after peek");

    // Pop all
    int out{};
    CHECK(s.pop(out) && out == 30, "pop 30");
    CHECK(!s.full(), "no longer full after one pop");
    CHECK(s.pop(out) && out == 20, "pop 20");
    CHECK(s.pop(out) && out == 10, "pop 10");
    CHECK(s.empty(), "stack is empty after popping all");

    // Underflow attempt
    CHECK(!s.pop(out), "pop fails when empty");
    CHECK(!s.peek(top), "peek fails when empty");
    dump_stack(s, "after popping all");
}

// Test 2: clear() behavior.
void test_clear()
{
    std::cout << "\n== Test 2: clear() resets state ==\n";
    stack<int, 4> s;
    CHECK(s.push(1), "push 1");
    CHECK(s.push(2), "push 2");
    dump_stack(s, "before clear");

    s.clear();
    CHECK(s.empty(), "empty after clear");
    CHECK(!s.full(), "not full after clear");
    CHECK(s.size() == 0, "size() == 0 after clear");
    dump_stack(s, "after clear");

    CHECK(s.push(99), "push works after clear");
    int top{};
    CHECK(s.peek(top) && top == 99, "peek sees 99 after clear");
    dump_stack(s, "after pushing 99 post-clear");
}

// Test 3: works with non-POD types (std::string).
void test_string()
{
    std::cout << "\n== Test 3: std::string usage ==\n";
    stack<std::string, 3> s;

    CHECK(s.push("alpha"),  "push 'alpha'");
    CHECK(s.push("beta"),   "push 'beta'");
    CHECK(s.push("gamma"),  "push 'gamma'");
    CHECK(s.full(),         "string stack is full");

    std::string top;
    CHECK(s.peek(top) && top == "gamma", "peek 'gamma'");
    dump_stack(s, "string stack after 3 pushes");

    std::string out;
    CHECK(s.pop(out) && out == "gamma", "pop 'gamma'");
    CHECK(s.pop(out) && out == "beta",  "pop 'beta'");
    CHECK(s.pop(out) && out == "alpha", "pop 'alpha'");
    CHECK(s.empty(), "string stack empty at end");
    dump_stack(s, "string stack after popping all");
}

// Test 4: light stress test—many push/pop cycles.
void test_stress()
{
    std::cout << "\n== Test 4: light stress cycles ==\n";
    stack<int, 5> s;

    // 100 cycles of fill-then-drain
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (int i = 0; i < 5; ++i) {
            bool ok = s.push(cycle * 10 + i);
            if (!ok) {
                std::cerr << "✘ unexpected push failure in cycle " << cycle << "\n";
                std::exit(1);
            }
        }
        CHECK(s.full(), "full after 5 pushes in cycle");

        for (int i = 4; i >= 0; --i) {
            int out{};
            bool ok = s.pop(out);
            if (!ok || out != cycle * 10 + i) {
                std::cerr << "✘ pop mismatch in cycle " << cycle
                          << " expected " << (cycle * 10 + i)
                          << " got " << out << "\n";
                std::exit(1);
            }
        }
        CHECK(s.empty(), "empty after draining in cycle");
    }
    std::cout << "✔ stress cycles completed\n";
}

int main()
{
    std::cout << "Running stack tests...\n";
    test_int_basic();
    test_clear();
    test_string();
    test_stress();
    std::cout << "\nAll tests passed. ✅\n";
    return 0;
}
