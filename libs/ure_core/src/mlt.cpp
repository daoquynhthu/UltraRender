#include "ure/mlt.hpp"

#include <limits>
#include <stdexcept>

namespace ure::integrator {

MltChainShard make_mlt_chain_shard(
    int shard_id, int shard_count, int local_chain_count,
    std::uint64_t base_chain_offset) {
    if (shard_count <= 0 || local_chain_count <= 0) {
        throw std::invalid_argument(
            "MLT shard count and local chain count must be positive");
    }
    if (shard_id < 0 || shard_id >= shard_count) {
        throw std::out_of_range("MLT shard id is outside the shard set");
    }
    const std::uint64_t local_count =
        static_cast<std::uint64_t>(local_chain_count);
    const std::uint64_t offset_delta =
        static_cast<std::uint64_t>(shard_id) * local_count;
    if (base_chain_offset >
        std::numeric_limits<std::uint64_t>::max() - offset_delta) {
        throw std::overflow_error("MLT global chain offset overflow");
    }
    return {shard_id, shard_count, local_chain_count,
        base_chain_offset + offset_delta};
}

bool validate_mlt_chain_shard(const MltChainShard& shard) {
    if (shard.shard_count <= 0 || shard.local_chain_count <= 0 ||
        shard.shard_id < 0 || shard.shard_id >= shard.shard_count) {
        return false;
    }
    const std::uint64_t local_count =
        static_cast<std::uint64_t>(shard.local_chain_count);
    return shard.global_chain_offset <=
        std::numeric_limits<std::uint64_t>::max() - local_count;
}

} // namespace ure::integrator
