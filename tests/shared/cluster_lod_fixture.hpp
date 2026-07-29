#pragma once

#include <array>

#include "ure/runtime/cluster_lod.hpp"

namespace ure::test {

struct ClusterLodFixture {
    std::array<runtime::ClusterVertex, 6> vertices;
    std::array<runtime::ClusterSourceTriangle, 2>
        triangles;

    ClusterLodFixture() {
        const auto make_vertex = [](
            float x,
            float y,
            float z) {
            runtime::ClusterVertex vertex;
            vertex.position = {x, y, z, 1.0f};
            vertex.normal = {0.0f, 0.0f, 1.0f, 0.0f};
            vertex.tangent_handedness = {
                1.0f, 0.0f, 0.0f, 1.0f};
            vertex.uv = {x, y, 0.0f, 0.0f};
            return vertex;
        };
        vertices = {
            make_vertex(-1.0f, -1.0f, 0.0f),
            make_vertex(0.5f, -1.0f, 0.0f),
            make_vertex(-0.25f, 1.0f, 0.0f),
            make_vertex(0.0f, -1.0f, 0.0f),
            make_vertex(1.5f, -1.0f, 0.0f),
            make_vertex(0.75f, 1.0f, 0.0f)};
        runtime::ClusterBoundaryKey boundary;
        boundary.material_slot = 3;
        boundary.material = {0x4d4154455249414cull, 21};
        boundary.spectral = {0x535045435452554dull, 22};
        boundary.displacement = {0x444953504c414345ull, 23};
        boundary.opacity = {0x4f50414349545900ull, 24};
        boundary.normal_field = {0x4e4f524d414c0000ull, 25};
        triangles[0] = {
            {0, 1, 2},
            100,
            boundary,
            {},
            9,
            0};
        triangles[1] = {
            {3, 4, 5},
            100,
            boundary,
            {1.0f, 0.1f, 0.05f, 0.001f, 0.001f},
            9,
            1};
    }

    runtime::ClusteredGeometryResource build() const {
        runtime::ClusterBuildOptions options;
        options.clusters_per_page = 1;
        options.required_page_count = 0;
        auto resource = runtime::build_clustered_geometry(
            {
                {0x434c4f4452455300ull, 31},
                {0x47454f4d45545259ull, 32},
                vertices,
                triangles},
            options);
        resource.clusters[0].parent_cluster = 1;
        static_cast<void>(
            runtime::validate_clustered_geometry(resource));
        return resource;
    }
};

inline runtime::ClusterResidencyState
make_complete_cluster_lod_residency(
    const runtime::ClusteredGeometryResource& resource) {
    auto residency =
        runtime::make_cluster_residency(resource);
    for (std::uint32_t page = 0;
         page < resource.pages.size();
         ++page) {
        runtime::set_cluster_page_resident(
            residency, page, true);
    }
    return residency;
}

#if defined(__CUDACC__)
__host__ __device__
#endif
inline runtime::ClusterLodQuery cluster_lod_query(
    runtime::ClusterPathClass path_class,
    float roughness = 0.5f) {
    runtime::ClusterLodQuery query;
    query.differential.origin_radius = 8.0f;
    query.differential.direction_spread = 0.01f;
    query.differential.distance = 4.0f;
    query.path_class = path_class;
    query.material_roughness = roughness;
    query.wavelength_span_nm = 200.0f;
    return query;
}

}
