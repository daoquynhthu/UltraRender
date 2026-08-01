#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

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

enum class HdUREMaterialLossSeverity {
    Warning,
    Error
};

struct HdUREMaterialLoss {
    HdUREMaterialLossSeverity severity =
        HdUREMaterialLossSeverity::Warning;
    std::string code;
    std::string path;
    std::string message;
};

struct HdUREMaterialRecord {
    std::string path;
    std::shared_ptr<const ure::scene_ir::MaterialNode>
        material;
    std::vector<HdUREMaterialLoss> loss_report;
    std::uint64_t revision = 0;
};

struct HdURERetainedScene {
    std::vector<HdUREMeshRecord> meshes;
    std::vector<HdUREMaterialRecord> materials;
    std::uint64_t revision = 0;
    std::uint64_t rejected_mesh_count = 0;
    std::uint64_t rejected_material_count = 0;
    std::string last_error;
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
    void UpdateMaterial(HdUREMaterialRecord record);
    void RejectMaterial(
        std::string path,
        std::vector<HdUREMaterialLoss> loss_report,
        std::string error);
    void RemoveMaterial(const std::string& path);
    std::optional<HdUREMaterialRecord>
    FindMaterial(const std::string& path) const;
    std::size_t MaterialCount() const;
    std::uint64_t RejectedMaterialCount() const;
    std::uint64_t MaterialLossCount() const;
    std::vector<HdUREMaterialLoss>
    FindMaterialLossReport(
        const std::string& path) const;
    std::string LastError() const;
    HdURERetainedScene SnapshotScene() const;
    void RecordRenderProgress(
        int spp,
        bool converged,
        std::uint64_t loss_count);
    void RecordRenderError(std::string error);
    int RenderSpp() const;
    bool RenderConverged() const;
    std::uint64_t RenderLossCount() const;
    std::string RenderError() const;

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
    std::map<std::string, HdUREMaterialRecord>
        materials_;
    std::map<std::string, RejectionRecord>
        rejected_materials_;
    std::map<
        std::string,
        std::vector<HdUREMaterialLoss>>
        material_loss_reports_;
    std::uint64_t next_revision_ = 1;
    std::uint64_t next_rejection_revision_ = 1;
    std::uint64_t scene_revision_ = 0;
    std::string last_error_;
    int render_spp_ = 0;
    bool render_converged_ = false;
    std::uint64_t render_loss_count_ = 0;
    std::string render_error_;
};

PXR_NAMESPACE_CLOSE_SCOPE
