#include <array>
#include <cstdio>
#include <stdexcept>

#include "ure/runtime/acceleration.hpp"

namespace rt = ure::runtime;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
bool throws_code(Function&& function, rt::ErrorCode code) {
    try {
        function();
    } catch (const rt::Error& error) {
        return error.code() == code;
    }
    return false;
}

rt::AccelerationSceneDesc valid_scene(
    std::span<const rt::AccelerationInstanceDesc> instances) {
    return {
        {
            rt::BufferHandle{1},
            0,
            16,
            3,
            rt::BufferHandle{2},
            0,
            3,
            rt::IndexFormat::Uint32,
            7},
        instances,
        "contract"};
}

}

int main() {
    try {
        const rt::AccelerationCapabilities native{
            rt::acceleration_feature_bit(
                rt::AccelerationFeature::ComputeBvh) |
                rt::acceleration_feature_bit(
                    rt::AccelerationFeature::RayQuery) |
                rt::acceleration_feature_bit(
                    rt::AccelerationFeature::RayTracingPipeline),
            1,
            1024};
        const auto automatic = rt::select_acceleration(native, {});
        require(
            automatic.mode == rt::AccelerationMode::RayQuery &&
                !automatic.fallback_used,
            "automatic acceleration did not prefer ray query");

        const rt::AccelerationCapabilities compute{
            rt::acceleration_feature_bit(
                rt::AccelerationFeature::ComputeBvh),
            1,
            1024};
        const auto fallback = rt::select_acceleration(
            compute,
            {
                rt::AccelerationMode::RayQuery,
                rt::AccelerationFallback::ComputeBvh});
        require(
            fallback.mode == rt::AccelerationMode::ComputeBvh &&
                fallback.fallback_used,
            "missing ray query did not select compute BVH fallback");
        require(
            throws_code(
                [&] {
                    static_cast<void>(rt::select_acceleration(
                        compute,
                        {
                            rt::AccelerationMode::RayQuery,
                            rt::AccelerationFallback::Reject}));
                },
                rt::ErrorCode::Unsupported),
            "explicit unavailable ray query was not rejected");

        std::array instances{
            rt::AccelerationInstanceDesc{}};
        instances[0].instance_index = 3;
        instances[0].material_index = 11;
        auto scene = valid_scene(instances);
        rt::validate(scene);

        auto invalid_geometry = scene;
        invalid_geometry.geometry.index_count = 4;
        require(
            throws_code(
                [&] { rt::validate(invalid_geometry); },
                rt::ErrorCode::InvalidArgument),
            "non-triangle index count was accepted");
        invalid_geometry = scene;
        invalid_geometry.geometry.index_offset = 2;
        require(
            throws_code(
                [&] { rt::validate(invalid_geometry); },
                rt::ErrorCode::InvalidArgument),
            "misaligned geometry offset was accepted");

        std::array duplicate_instances{
            rt::AccelerationInstanceDesc{},
            rt::AccelerationInstanceDesc{}};
        auto duplicate_scene = valid_scene(duplicate_instances);
        require(
            throws_code(
                [&] { rt::validate(duplicate_scene); },
                rt::ErrorCode::InvalidArgument),
            "duplicate instance index was accepted");
        instances[0].object_to_world[0] = 0.0f;
        instances[0].object_to_world[1] = 0.0f;
        instances[0].object_to_world[2] = 0.0f;
        require(
            throws_code(
                [&] { rt::validate(valid_scene(instances)); },
                rt::ErrorCode::InvalidArgument),
            "singular instance transform was accepted");

        rt::DispatchGraph graph{{
            {
                1,
                {},
                rt::DispatchCommand{
                    rt::PipelineHandle{1},
                    {1, 1, 1},
                    {rt::AccelerationBinding{
                        0,
                        rt::AccelerationSceneHandle{1}}}}}
        }, "acceleration-binding"};
        rt::validate(graph);
        std::get<rt::AccelerationBinding>(
            std::get<rt::DispatchCommand>(
                graph.nodes[0].command).bindings[0]).scene = {};
        require(
            throws_code(
                [&] { rt::validate(graph); },
                rt::ErrorCode::InvalidHandle),
            "invalid acceleration binding was accepted");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
