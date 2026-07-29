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

std::string HdURERenderParam::LastError() const {
    std::shared_lock lock(mutex_);
    return last_error_;
}

PXR_NAMESPACE_CLOSE_SCOPE
