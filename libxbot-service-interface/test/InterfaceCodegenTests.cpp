//
// Checks on the interface generated from codegen/templates/service.json.
//
// The frame budget helpers are constexpr, so the checks are static_asserts: this file passes by
// compiling. Linking it is what builds the generated interface source.
//

#include <ServiceTemplateInterfaceBase.hpp>

namespace {
using xbot::datatypes::DataDescriptor;
using xbot::datatypes::XbotHeader;

constexpr size_t kBody = xbot::config::max_packet_size - sizeof(XbotHeader);

// RemainingBudget is protected: it exists for the generated helpers.
struct Budget : xbot::serviceif::ServiceInterfaceBase {
  using ServiceInterfaceBase::RemainingBudget;
};

// A saturating subtraction, for any number of subtrahends.
static_assert(Budget::RemainingBudget(10, 3) == 7);
static_assert(Budget::RemainingBudget(10, 10) == 0);
static_assert(Budget::RemainingBudget(10, 11) == 0);
static_assert(Budget::RemainingBudget(100, 10, 20, 30) == 40);
static_assert(Budget::RemainingBudget(100, 10, 200, 30) == 0);  // stays at 0, does not wrap back up

// A blob parameter may use what one packet has left after the framing of its call.
static_assert(ServiceTemplateInterfaceBase::MaxBlobParamsWithBlobReturnData() == kBody - sizeof(DataDescriptor));

// A string parameter is limited by its max_length (64) as well: Call...() refuses a longer value,
// so a helper that reported the packet budget would send the caller into a call that always fails.
static_assert(ServiceTemplateInterfaceBase::MaxStringParamsWithStringReturnPath() == 64);
}  // namespace

int main() {
  return 0;
}
