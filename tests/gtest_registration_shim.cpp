// Wrapper shim to bridge mismatch between test objects expecting
// testing::internal::MakeAndRegisterTestInfo taking a std::string
// as the first parameter and the GoogleTest library we built which
// only provides the const char* overload. This function simply
// forwards to the const char* version. If upstream headers change
// and already provide this overload, the duplicate should be
// avoided by the one-definition rule across translation units; in
// practice this file can be removed once root cause of mismatch is fixed.

#include <string>
#include <gtest/gtest.h>

namespace testing {
namespace internal {

// Forward declaration of the existing overload (char const* first param)
TestInfo* MakeAndRegisterTestInfo(const char* test_suite_name,
                                  const char* name,
                                  const char* type_param,
                                  const char* value_param,
                                  CodeLocation code_location,
                                  const void* fixture_class_id,
                                  void (*set_up)(),
                                  void (*tear_down)(),
                                  TestFactoryBase* factory);

// New overload matching what the objects appear to reference (std::string first param)
TestInfo* MakeAndRegisterTestInfo(std::string test_suite_name,
                                  const char* name,
                                  const char* type_param,
                                  const char* value_param,
                                  CodeLocation code_location,
                                  const void* fixture_class_id,
                                  void (*set_up)(),
                                  void (*tear_down)(),
                                  TestFactoryBase* factory) {
    return MakeAndRegisterTestInfo(test_suite_name.c_str(), name, type_param, value_param,
                                   code_location, fixture_class_id, set_up, tear_down, factory);
}

} // namespace internal
} // namespace testing
