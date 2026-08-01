#include <mutex>
#include <utility>

#include "render_param.hpp"

PXR_NAMESPACE_OPEN_SCOPE

void HdURERenderParam::RefreshLastErrorLocked() {
    std::uint64_t latest_revision = 0;
    last_error_.clear();
    for (const auto& [path, rejection] :
         rejected_meshes_) {
        static_cast<void>(path);
        if (rejection.revision >
            latest_revision) {
            latest_revision = rejection.revision;
            last_error_ = rejection.error;
        }
    }
    for (const auto& [path, rejection] :
         rejected_materials_) {
        static_cast<void>(path);
        if (rejection.revision >
            latest_revision) {
            latest_revision = rejection.revision;
            last_error_ = rejection.error;
        }
    }
}

void HdURERenderParam::UpdateMesh(
    HdUREMeshRecord record) {
    std::unique_lock lock(mutex_);
    const std::string path = record.path;
    record.revision = next_revision_++;
    meshes_.insert_or_assign(
        path,
        std::move(record));
    rejected_meshes_.erase(path);
    RefreshLastErrorLocked();
}

void HdURERenderParam::RejectMesh(
    std::string path,
    std::string error) {
    std::unique_lock lock(mutex_);
    meshes_.erase(path);
    const std::string last_error = error;
    rejected_meshes_.insert_or_assign(
        std::move(path),
        RejectionRecord{
            next_rejection_revision_++,
            std::move(error)});
    last_error_ = last_error;
}

void HdURERenderParam::RemoveMesh(
    const std::string& path) {
    std::unique_lock lock(mutex_);
    meshes_.erase(path);
    rejected_meshes_.erase(path);
    RefreshLastErrorLocked();
}

std::optional<HdUREMeshRecord>
HdURERenderParam::FindMesh(
    const std::string& path) const {
    std::shared_lock lock(mutex_);
    const auto found = meshes_.find(path);
    return found == meshes_.end()
        ? std::nullopt
        : std::optional<HdUREMeshRecord>(
              found->second);
}

std::size_t HdURERenderParam::MeshCount() const {
    std::shared_lock lock(mutex_);
    return meshes_.size();
}

std::uint64_t
HdURERenderParam::RejectedMeshCount() const {
    std::shared_lock lock(mutex_);
    return rejected_meshes_.size();
}

void HdURERenderParam::UpdateMaterial(
    HdUREMaterialRecord record) {
    std::unique_lock lock(mutex_);
    const std::string path = record.path;
    record.revision = next_revision_++;
    materials_.insert_or_assign(
        path,
        std::move(record));
    material_loss_reports_.insert_or_assign(
        path,
        materials_.at(path).loss_report);
    rejected_materials_.erase(path);
    RefreshLastErrorLocked();
}

void HdURERenderParam::RejectMaterial(
    std::string path,
    std::vector<HdUREMaterialLoss> loss_report,
    std::string error) {
    std::unique_lock lock(mutex_);
    materials_.erase(path);
    material_loss_reports_.insert_or_assign(
        path,
        std::move(loss_report));
    const std::string last_error = error;
    rejected_materials_.insert_or_assign(
        std::move(path),
        RejectionRecord{
            next_rejection_revision_++,
            std::move(error)});
    last_error_ = last_error;
}

void HdURERenderParam::RemoveMaterial(
    const std::string& path) {
    std::unique_lock lock(mutex_);
    materials_.erase(path);
    rejected_materials_.erase(path);
    material_loss_reports_.erase(path);
    RefreshLastErrorLocked();
}

std::optional<HdUREMaterialRecord>
HdURERenderParam::FindMaterial(
    const std::string& path) const {
    std::shared_lock lock(mutex_);
    const auto found = materials_.find(path);
    return found == materials_.end()
        ? std::nullopt
        : std::optional<HdUREMaterialRecord>(
              found->second);
}

std::size_t
HdURERenderParam::MaterialCount() const {
    std::shared_lock lock(mutex_);
    return materials_.size();
}

std::uint64_t
HdURERenderParam::RejectedMaterialCount() const {
    std::shared_lock lock(mutex_);
    return rejected_materials_.size();
}

std::uint64_t
HdURERenderParam::MaterialLossCount() const {
    std::shared_lock lock(mutex_);
    std::uint64_t count = 0;
    for (const auto& [path, loss_report] :
         material_loss_reports_) {
        static_cast<void>(path);
        count += loss_report.size();
    }
    return count;
}

std::vector<HdUREMaterialLoss>
HdURERenderParam::FindMaterialLossReport(
    const std::string& path) const {
    std::shared_lock lock(mutex_);
    const auto found =
        material_loss_reports_.find(path);
    return found ==
            material_loss_reports_.end()
        ? std::vector<HdUREMaterialLoss>{}
        : found->second;
}

std::string HdURERenderParam::LastError() const {
    std::shared_lock lock(mutex_);
    return last_error_;
}

PXR_NAMESPACE_CLOSE_SCOPE
