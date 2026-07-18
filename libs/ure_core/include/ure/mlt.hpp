#pragma once

#include <cstdint>

namespace ure::integrator {

struct MltChainShard {
    int shard_id = 0;
    int shard_count = 1;
    int local_chain_count = 0;
    std::uint64_t global_chain_offset = 0;
};

MltChainShard make_mlt_chain_shard(
    int shard_id, int shard_count, int local_chain_count,
    std::uint64_t base_chain_offset = 0);

bool validate_mlt_chain_shard(const MltChainShard& shard);

} // namespace ure::integrator
