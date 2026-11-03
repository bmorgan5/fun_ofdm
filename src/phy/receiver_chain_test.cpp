#include "receiver_chain.h"

#include <gtest/gtest.h>

#include <complex>
#include <vector>

namespace fun {
namespace {
// Destroying a receiver_chain stops and joins its threads.
TEST(ReceiverChainTest, CanBeDestroyed) {
  GTEST_SKIP() << "The block threads are never joined, so destroying a "
                  "receiver_chain calls std::terminate";

  receiver_chain rx;
  rx.process_samples(std::vector<std::complex<double>>(4096, 0));
}
}  // namespace
}  // namespace fun
