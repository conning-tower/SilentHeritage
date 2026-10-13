// Test cases registered at run time, one per item of a list, so that CTest
// (`catch_discover_tests`) runs each in its own process, in parallel with
// the others: for the long checks repeated over every improvement flag.
//
//   [[maybe_unused]] const bool registered = register_test_cases(
//       kItems, [](const Item& i) { return "Each item works: " + i.name; }, "[tag]",
//       [](const Item& i) { CHECK(works(i)); });
#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_test_invoker.hpp>

#include <functional>
#include <string>
#include <utility>

namespace sh::test {

// Runs `body` as a test case.
class FunctionInvoker : public Catch::ITestInvoker {
 public:
  explicit FunctionInvoker(std::function<void()> body) : body_(std::move(body)) {}
  void invoke() const override { body_(); }

 private:
  std::function<void()> body_;
};

// Registers a test case named `name(item)` with `tags` running `body(item)`
// for each item of `items`, which must outlive the run. Returns true, to
// be called in a namespace-scope initializer (as Catch's TEST_CASE
// registers).
template <typename Items, typename NameFn, typename BodyFn>
bool register_test_cases(const Items& items, NameFn name, const char* tags, BodyFn body,
                         const Catch::SourceLineInfo& where = Catch::SourceLineInfo(__builtin_FILE(),
                                                                                     __builtin_LINE())) {
  for (const auto& item : items) {
    const std::string test_name = name(item);
    Catch::AutoReg(Catch::Detail::make_unique<FunctionInvoker>([&item, body] { body(item); }), where,
                   Catch::StringRef(), Catch::NameAndTags(test_name, tags));
  }
  return true;
}

}  // namespace sh::test
