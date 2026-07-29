#pragma once

#include <array>
#include <cstdint>

#include "ure/runtime/clustered_geometry.hpp"

namespace ure::test {

inline runtime::ClusterVertex cluster_vertex(
    float x,
    float y,
    float z) {
    runtime::ClusterVertex value;
    value.position = {x, y, z, 1.0f};
    value.normal = {0.0f, 0.0f, 1.0f, 0.0f};
    value.tangent_handedness = {
        1.0f, 0.0f, 0.0f, 1.0f};
    value.uv = {x, y, 0.0f, 0.0f};
    return value;
}

inline runtime::ClusterBoundaryKey cluster_boundary(
    std::uint32_t material_slot,
    std::uint64_t local_id) {
    runtime::ClusterBoundaryKey value;
    value.material_slot = material_slot;
    value.material = {0x4d4154455249414cull, local_id};
    value.spectral = {0x535045435452554dull, local_id};
    return value;
}

struct ClusteredGeometryFixture {
    std::array<runtime::ClusterVertex, 6> vertices = {
        cluster_vertex(0.0f, 0.0f, 0.0f),
        cluster_vertex(1.0f, 0.0f, 0.0f),
        cluster_vertex(1.0f, 1.0f, 0.0f),
        cluster_vertex(0.0f, 1.0f, 0.0f),
        cluster_vertex(2.0f, 0.0f, 0.0f),
        cluster_vertex(2.0f, 1.0f, 0.0f)};
    std::array<runtime::ClusterSourceTriangle, 4>
        triangles;

    ClusteredGeometryFixture() {
        const auto first = cluster_boundary(0, 11);
        const auto second = cluster_boundary(1, 17);
        triangles[0] = {{0, 1, 2}, 41, first};
        triangles[1] = {{0, 2, 3}, 42, first};
        triangles[2] = {{1, 4, 5}, 43, second};
        triangles[3] = {{1, 5, 2}, 44, first};
    }

    runtime::ClusterBuildInput input() const {
        return {
            {0x434c555354455200ull, 9},
            {0x47454f4d45545259ull, 3},
            vertices,
            triangles};
    }
};

inline runtime::ClusteredGeometryResource
build_clustered_geometry_fixture() {
    const ClusteredGeometryFixture fixture;
    runtime::ClusterBuildOptions options;
    options.clusters_per_page = 2;
    options.required_page_count = 1;
    return runtime::build_clustered_geometry(
        fixture.input(), options);
}

}
