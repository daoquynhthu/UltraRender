#pragma once

#include "ure/distributed_contract.hpp"

#include <filesystem>
#include <vector>

namespace ure::gpu {

struct DistributedFrameBufferStorage {
    int width = 0;
    int height = 0;
    int total_samples = 0;
    std::vector<float> data;

    DistributedFrameBuffer view();
    DistributedFrameBuffer view() const = delete;
};

void write_sample_range_file(const std::filesystem::path& path,
                             const DistributedSampleRange& range);

DistributedSampleRange read_sample_range_file(const std::filesystem::path& path);

void write_framebuffer_file(const std::filesystem::path& path,
                            const DistributedFrameBuffer& framebuffer);

DistributedFrameBufferStorage read_framebuffer_file(const std::filesystem::path& path);

void merge_framebuffer_files(const std::filesystem::path& accum_path,
                             const std::vector<std::filesystem::path>& incoming_paths,
                             const std::filesystem::path& output_path);

} // namespace ure::gpu
