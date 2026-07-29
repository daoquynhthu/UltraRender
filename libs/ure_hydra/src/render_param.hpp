#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>

#include <pxr/imaging/hd/renderDelegate.h>

#include <ure/scene_ir.hpp>

PXR_NAMESPACE_OPEN_SCOPE

struct HdUREMeshRecord {
    std::string path;
    std::shared_ptr<const ure::scene_ir::MeshResource>
        geometry;
    std::array<double, 16> transform = {};
    std::string material_path;
    bool visible = true;
    bool double_sided = false;
    std::uint64_t revision = 0;
};

class HdURERenderParam final
    : public HdRenderParam {
public:
    void UpdateMesh(HdUREMeshRecord record);
    void RejectMesh(
        std::string path,
        std::string error);
    void RemoveMesh(const std::string& path);
    std::optional<HdUREMeshRecord>
    FindMesh(const std::string& path) const;
    std::size_t MeshCount() const;
    std::uint64_t RejectedMeshCount() const;
    std::string LastError() const;

private:
    struct RejectionRecord {
        std::uint64_t revision = 0;
        std::string error;
    };

    void RefreshLastErrorLocked();

    mutable std::shared_mutex mutex_;
    std::map<std::string, HdUREMeshRecord> meshes_;
    std::map<std::string, RejectionRecord>
        rejected_meshes_;
    std::uint64_t next_revision_ = 1;
    std::uint64_t next_rejection_revision_ = 1;
    std::string last_error_;
};

PXR_NAMESPACE_CLOSE_SCOPE
