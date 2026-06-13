#include "ure/distributed_contract.hpp"
#include <cassert>

namespace ure::gpu {

void merge_partial_framebuffer(DistributedFrameBuffer& accum,
                               const DistributedFrameBuffer& incoming) {
    assert(accum.width == incoming.width);
    assert(accum.height == incoming.height);
    assert(accum.data && incoming.data);

    int count = accum.width * accum.height * 3;
    for (int i = 0; i < count; ++i) {
        accum.data[i] += incoming.data[i];
    }
    accum.total_samples += incoming.total_samples;
}

} // namespace ure::gpu
