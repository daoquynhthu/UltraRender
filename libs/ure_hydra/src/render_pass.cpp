#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/tokens.h>

#include <ure/session.hpp>

#include "render_buffer.hpp"
#include "render_param.hpp"
#include "render_pass.hpp"
#include "scene_snapshot.hpp"

PXR_NAMESPACE_OPEN_SCOPE
namespace {

std::uint64_t hash_combine(
    std::uint64_t hash,
    std::uint64_t value) {
    return (hash ^ value) *
        1099511628211ull;
}

std::uint64_t camera_hash(
    const HdCamera& camera,
    int width,
    int height) {
    std::uint64_t result =
        1469598103934665603ull;
    const GfMatrix4d& transform =
        camera.GetTransform();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0;
             column < 4;
             ++column) {
            result = hash_combine(
                result,
                std::hash<double>{}(
                    transform[row][column]));
        }
    }
    for (const float value : {
             camera.GetHorizontalAperture(),
             camera.GetVerticalAperture(),
             camera.GetFocalLength(),
             camera.GetFStop(),
             camera.GetFocusDistance()}) {
        result = hash_combine(
            result,
            std::hash<float>{}(value));
    }
    result = hash_combine(
        result,
        static_cast<std::uint64_t>(width));
    return hash_combine(
        result,
        static_cast<std::uint64_t>(height));
}

ure::core::Vec3f vector_value(
    const GfVec3d& value) {
    return {
        static_cast<float>(value[0]),
        static_cast<float>(value[1]),
        static_cast<float>(value[2])};
}

ure::Camera camera_value(
    const HdCamera& source,
    int width,
    int height) {
    if (source.GetProjection() !=
        HdCamera::Perspective) {
        throw std::runtime_error(
            "Hydra orthographic cameras require a native camera model");
    }
    if (std::abs(
            source.GetHorizontalApertureOffset()) >
            1.0e-6f ||
        std::abs(
            source.GetVerticalApertureOffset()) >
            1.0e-6f) {
        throw std::runtime_error(
            "Hydra camera aperture offsets are unsupported");
    }
    const float focal_length =
        source.GetFocalLength();
    const float vertical_aperture =
        source.GetVerticalAperture();
    if (!std::isfinite(focal_length) ||
        !std::isfinite(vertical_aperture) ||
        focal_length <= 0.0f ||
        vertical_aperture <= 0.0f) {
        throw std::runtime_error(
            "Hydra camera lens parameters are invalid");
    }
    const GfMatrix4d& transform =
        source.GetTransform();
    const double determinant =
        transform.GetDeterminant3();
    if (!std::isfinite(determinant) ||
        determinant <= 1.0e-12 ||
        std::abs(transform[0][3]) > 1.0e-12 ||
        std::abs(transform[1][3]) > 1.0e-12 ||
        std::abs(transform[2][3]) > 1.0e-12 ||
        std::abs(transform[3][3] - 1.0) >
            1.0e-12) {
        throw std::runtime_error(
            "Hydra camera transform must be a finite right-handed affine transform");
    }
    const GfVec3d position =
        transform.Transform(GfVec3d(0.0));
    const GfVec3d forward =
        transform.TransformDir(
            GfVec3d(0.0, 0.0, -1.0))
            .GetNormalized();
    const GfVec3d up =
        transform.TransformDir(
            GfVec3d(0.0, 1.0, 0.0))
            .GetNormalized();
    if (!std::isfinite(position[0]) ||
        !std::isfinite(position[1]) ||
        !std::isfinite(position[2]) ||
        !std::isfinite(forward[0]) ||
        !std::isfinite(forward[1]) ||
        !std::isfinite(forward[2]) ||
        !std::isfinite(up[0]) ||
        !std::isfinite(up[1]) ||
        !std::isfinite(up[2]) ||
        forward.GetLengthSq() <= 1.0e-12 ||
        up.GetLengthSq() <= 1.0e-12 ||
        std::abs(GfDot(forward, up)) > 0.999999) {
        throw std::runtime_error(
            "Hydra camera transform is invalid");
    }
    ure::Camera result;
    result.position = vector_value(position);
    result.look_at = vector_value(
        position + forward);
    result.up = vector_value(up);
    result.fov =
        2.0f * std::atan(
            vertical_aperture /
            (2.0f * focal_length)) *
        57.29577951308232f;
    result.aspect_ratio =
        static_cast<float>(width) /
        static_cast<float>(height);
    const float f_stop = source.GetFStop();
    const float focus_distance =
        source.GetFocusDistance();
    if (!std::isfinite(f_stop) || f_stop < 0.0f ||
        !std::isfinite(focus_distance) ||
        focus_distance < 0.0f) {
        throw std::runtime_error(
            "Hydra camera depth-of-field parameters are invalid");
    }
    result.aperture =
        std::isfinite(f_stop) && f_stop > 0.0f
        ? focal_length / (20.0f * f_stop)
        : 0.0f;
    result.focus_dist =
        focus_distance > 0.0f
        ? focus_distance
        : 1.0f;
    return result;
}

HdURERenderBuffer* render_buffer(
    HdRenderIndex& index,
    const HdRenderPassAovBinding& binding) {
    HdRenderBuffer* buffer = binding.renderBuffer;
    if (!buffer && !binding.renderBufferId.IsEmpty()) {
        buffer = dynamic_cast<HdRenderBuffer*>(
            index.GetBprim(
                HdPrimTypeTokens->renderBuffer,
                binding.renderBufferId));
    }
    auto* result =
        dynamic_cast<HdURERenderBuffer*>(buffer);
    if (!result) {
        throw std::runtime_error(
            "Hydra AOV binding does not reference an HdURE render buffer");
    }
    return result;
}

ure::AovType aov_type(
    const TfToken& name) {
    if (name == HdAovTokens->color) {
        return ure::AovType::Beauty;
    }
    if (name == HdAovTokens->normal) {
        return ure::AovType::Normal;
    }
    if (name == HdAovTokens->depth ||
        name == HdAovTokens->cameraDepth) {
        return ure::AovType::Depth;
    }
    if (name == TfToken("albedo")) {
        return ure::AovType::Albedo;
    }
    if (name == TfToken("uv")) {
        return ure::AovType::Uv;
    }
    if (name == TfToken("motionVector")) {
        return ure::AovType::MotionVector;
    }
    throw std::runtime_error(
        "Hydra requested an unsupported AOV: " +
        name.GetString());
}

}

HdURERenderPass::HdURERenderPass(
    HdRenderIndex* index,
    const HdRprimCollection& collection,
    HdURERenderParam* render_param,
    ure::RenderConfig config,
    int max_spp)
    : HdRenderPass(index, collection),
      render_param_(render_param),
      config_(std::move(config)),
      max_spp_(max_spp) {
    if (!render_param_ || max_spp_ <= 0) {
        throw std::invalid_argument(
            "HdURE render pass configuration is invalid");
    }
}

HdURERenderPass::~HdURERenderPass() = default;

bool HdURERenderPass::IsConverged() const {
    return converged_ || failed_;
}

void HdURERenderPass::_Execute(
    const HdRenderPassStateSharedPtr& state,
    const TfTokenVector& render_tags) {
    std::vector<HdURERenderBuffer*> buffers;
    try {
        if (!state || !state->GetCamera()) {
            throw std::runtime_error(
                "Hydra render pass requires a synchronized camera");
        }
        for (const auto& tag : render_tags) {
            if (tag != HdRenderTagTokens->geometry) {
                throw std::runtime_error(
                    "Hydra render tag is unsupported: " +
                    tag.GetString());
            }
        }
        const auto& bindings =
            state->GetAovBindings();
        if (bindings.empty()) {
            throw std::runtime_error(
                "Hydra render pass has no AOV bindings");
        }
        HdRenderIndex* index = GetRenderIndex();
        if (!index) {
            throw std::runtime_error(
                "Hydra render pass has no render index");
        }
        for (const auto& binding : bindings) {
            buffers.push_back(
                render_buffer(*index, binding));
        }
        const int width = static_cast<int>(
            buffers.front()->GetWidth());
        const int height = static_cast<int>(
            buffers.front()->GetHeight());
        if (width <= 0 || height <= 0 ||
            std::ranges::any_of(
                buffers,
                [&](const auto* buffer) {
                    return buffer->GetWidth() !=
                               static_cast<unsigned int>(width) ||
                           buffer->GetHeight() !=
                               static_cast<unsigned int>(height);
                })) {
            throw std::runtime_error(
                "Hydra AOV buffers have incompatible dimensions");
        }
        std::vector<ure::AovType> aov_types;
        aov_types.reserve(bindings.size());
        for (std::size_t index_value = 0;
             index_value < bindings.size();
             ++index_value) {
            const ure::AovType type =
                aov_type(
                    bindings[index_value].aovName);
            const std::size_t source_channels =
                static_cast<std::size_t>(
                    ure::aov_channel_count(type));
            const std::size_t target_channels =
                HdGetComponentCount(
                    buffers[index_value]->GetFormat());
            if (target_channels != source_channels &&
                !(type == ure::AovType::Beauty &&
                  source_channels == 3 &&
                  target_channels == 4)) {
                throw std::runtime_error(
                    "Hydra AOV format has an incompatible channel count");
            }
            aov_types.push_back(type);
        }
        const ure::Camera camera =
            camera_value(
                *state->GetCamera(),
                width,
                height);
        const std::uint64_t next_camera_hash =
            camera_hash(
                *state->GetCamera(),
                width,
                height);
        const HdURERetainedScene retained =
            render_param_->SnapshotScene();
        const bool rebuild =
            !session_ || collection_dirty_ ||
            retained.revision != scene_revision_ ||
            width != width_ || height != height_;
        if (rebuild) {
            auto snapshot = BuildSceneSnapshot(
                retained,
                GetRprimCollection(),
                camera,
                width,
                height);
            session_ = std::make_unique<
                ure::RenderSession>(
                ure::RenderSession::create(
                    config_));
            session_->load_scene(snapshot.scene);
            scene_revision_ = snapshot.revision;
            loss_count_ = snapshot.loss_report.size();
            width_ = width;
            height_ = height;
            collection_dirty_ = false;
            camera_hash_ = next_camera_hash;
        } else if (
            next_camera_hash != camera_hash_) {
            session_->update_camera(camera);
            camera_hash_ = next_camera_hash;
        }
        const int spp = session_->render_pass();
        converged_ = spp >= max_spp_;
        for (std::size_t index_value = 0;
             index_value < bindings.size();
             ++index_value) {
            const ure::AovType type =
                aov_types[index_value];
            const std::size_t source_channels =
                static_cast<std::size_t>(
                    ure::aov_channel_count(type));
            const auto& values =
                type == ure::AovType::Beauty
                ? session_->get_framebuffer()
                : session_->get_aov(type);
            buffers[index_value]->Write(
                values,
                source_channels,
                converged_);
        }
        render_param_->RecordRenderProgress(
            spp,
            converged_,
            loss_count_);
        failed_ = false;
    } catch (const std::exception& error) {
        failed_ = true;
        converged_ = true;
        for (auto* buffer : buffers) {
            buffer->SetConverged(true);
        }
        render_param_->RecordRenderError(
            error.what());
        TF_RUNTIME_ERROR(
            "HdURE render pass failed: %s",
            error.what());
    }
}

void HdURERenderPass::_MarkCollectionDirty() {
    collection_dirty_ = true;
    converged_ = false;
    failed_ = false;
}

PXR_NAMESPACE_CLOSE_SCOPE
