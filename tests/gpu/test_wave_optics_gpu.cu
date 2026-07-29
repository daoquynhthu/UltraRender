#include <cmath>
#include <cstdio>
#include <algorithm>
#include <memory>
#include <vector>

#include "test_framework.cuh"
#include "ure/detail/cuda_context.cuh"
#include "ure/detail/cuda_driver.cuh"
#include "ure/detail/cuda_scene_compiler.hpp"
#include "ure/detail/cuda_structs.cuh"
#include "ure/render.hpp"
#include "ure/scene_ir.hpp"
#include "ure/wave_optics.hpp"
#include "ure/anisotropic_optics.hpp"
#include "ure/local_fullwave.hpp"

namespace ure::gpu {

#include "path_tracer_decl.cuh"
#include "path_tracer_boundary.cuh"
#include "path_tracer_diffractive_jones.cuh"

__global__ void apply_diffractive_jones_test_kernel(
    StokesVector input,
    DiffractiveJonesMatrix matrix,
    StokesVector* output) {
    *output =
        apply_diffractive_jones(input, matrix);
}

}

static ure::gpu::StokesVector apply_gpu_jones(
    ure::gpu::StokesVector input,
    ure::gpu::DiffractiveJonesMatrix matrix) {
    ure::gpu::StokesVector* device_output = nullptr;
    const cudaError_t allocation = cudaMalloc(
            reinterpret_cast<void**>(&device_output),
            sizeof(*device_output));
    if (allocation != cudaSuccess) {
        return {};
    }
    DeviceMem guard(device_output);
    ure::gpu::apply_diffractive_jones_test_kernel<<<1, 1>>>(
        input,
        matrix,
        device_output);
    ure::gpu::StokesVector output;
    const cudaError_t synchronized =
        cudaDeviceSynchronize();
    const cudaError_t copied =
        synchronized == cudaSuccess
        ? cudaMemcpy(
            &output,
            device_output,
            sizeof(output),
            cudaMemcpyDeviceToHost)
        : synchronized;
    if (synchronized != cudaSuccess ||
        copied != cudaSuccess) {
        return {};
    }
    return output;
}

static int test_gpu_diffractive_jones_response() {
    REQUIRE_GPU();
    ure::gpu::DiffractiveJonesMatrix identity;
    identity.ss = {1.0f, 0.0f};
    identity.pp = {1.0f, 0.0f};
    const ure::gpu::StokesVector input(
        1.0f,
        0.2f,
        0.3f,
        0.4f);
    const auto unchanged =
        apply_gpu_jones(input, identity);
    CHECK_FLOAT_EQ(unchanged.I, input.I, 1.0e-6f);
    CHECK_FLOAT_EQ(unchanged.Q, input.Q, 1.0e-6f);
    CHECK_FLOAT_EQ(unchanged.U, input.U, 1.0e-6f);
    CHECK_FLOAT_EQ(unchanged.V, input.V, 1.0e-6f);

    ure::gpu::DiffractiveJonesMatrix polarizer;
    polarizer.ss = {1.0f, 0.0f};
    const auto polarized =
        apply_gpu_jones(
            ure::gpu::StokesVector(
                1.0f,
                0.0f,
                0.0f,
                0.0f),
            polarizer);
    CHECK_FLOAT_EQ(polarized.I, 0.5f, 1.0e-6f);
    CHECK_FLOAT_EQ(polarized.Q, 0.5f, 1.0e-6f);
    CHECK_FLOAT_EQ(polarized.U, 0.0f, 1.0e-6f);
    CHECK_FLOAT_EQ(polarized.V, 0.0f, 1.0e-6f);

    ure::gpu::DiffractiveJonesMatrix quarter_wave;
    quarter_wave.ss = {1.0f, 0.0f};
    quarter_wave.pp = {0.0f, 1.0f};
    const auto circular =
        apply_gpu_jones(
            ure::gpu::StokesVector(
                1.0f,
                0.0f,
                1.0f,
                0.0f),
            quarter_wave);
    CHECK_FLOAT_EQ(circular.I, 1.0f, 1.0e-6f);
    CHECK_FLOAT_EQ(circular.Q, 0.0f, 1.0e-6f);
    CHECK_FLOAT_EQ(circular.U, 0.0f, 1.0e-6f);
    CHECK_FLOAT_EQ(circular.V, -1.0f, 1.0e-6f);
    return 0;
}

static int test_gpu_fraunhofer_uniform_field() {
    REQUIRE_GPU();

    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 2.0e-6;
    field.wavelength_m = 550.0e-9;
    field.samples.assign(16, {1.0, 0.0});

    const auto gpu = ure::wave::propagate_fraunhofer_gpu(field);
    CHECK(gpu.width == 4);
    CHECK(gpu.height == 4);
    CHECK(gpu.amplitudes.size() == 16);
    CHECK_FLOAT_EQ(static_cast<float>(gpu.frequency_pitch_x_cycles_per_m),
                   static_cast<float>(1.0 / (4.0 * field.sample_pitch_m)),
                   1.0e-3f);

    CHECK_FLOAT_EQ(static_cast<float>(gpu.at(2, 2).real), 16.0f, 1.0e-4f);
    CHECK_FLOAT_EQ(static_cast<float>(gpu.at(2, 2).imag), 0.0f, 1.0e-4f);
    CHECK_FLOAT_EQ(static_cast<float>(gpu.intensity_at(2, 2)), 256.0f, 1.0e-3f);
    for (int y = 0; y < gpu.height; ++y) {
        for (int x = 0; x < gpu.width; ++x) {
            if (x == 2 && y == 2) continue;
            CHECK_FLOAT_EQ(static_cast<float>(gpu.intensity_at(x, y)), 0.0f, 1.0e-8f);
        }
    }
    return 0;
}

static int test_gpu_fraunhofer_matches_cpu_reference() {
    REQUIRE_GPU();

    ure::wave::CircularPupil pupil;
    pupil.aperture.wavelength_m = 550.0e-9;
    pupil.aperture.aperture_diameter_m = 2.0e-3;
    pupil.aperture.focal_length_m = 35.0e-3;
    pupil.defocus_waves_at_edge = 0.125;

    const auto field = ure::wave::make_circular_pupil_field(pupil, 7);
    const auto cpu = ure::wave::propagate_fraunhofer_direct(field);
    const auto gpu = ure::wave::propagate_fraunhofer_gpu(field);
    CHECK(gpu.width == cpu.width);
    CHECK(gpu.height == cpu.height);
    CHECK(gpu.amplitudes.size() == cpu.amplitudes.size());

    for (std::size_t i = 0; i < cpu.amplitudes.size(); ++i) {
        CHECK_FLOAT_EQ(static_cast<float>(gpu.amplitudes[i].real),
                       static_cast<float>(cpu.amplitudes[i].real),
                       1.0e-4f);
        CHECK_FLOAT_EQ(static_cast<float>(gpu.amplitudes[i].imag),
                       static_cast<float>(cpu.amplitudes[i].imag),
                       1.0e-4f);
    }
    return 0;
}

static int test_gpu_fraunhofer_invalid_fails_closed() {
    ure::wave::WaveFieldGrid field;
    field.width = 4;
    field.height = 4;
    field.sample_pitch_m = 0.0;
    field.wavelength_m = 550.0e-9;
    field.samples.assign(16, {1.0, 0.0});

    const auto gpu = ure::wave::propagate_fraunhofer_gpu(field);
    CHECK(gpu.width == 0);
    CHECK(gpu.amplitudes.empty());
    return 0;
}

static ure::scene_ir::SceneIR make_diffraction_scene() {
    ure::scene_ir::SceneIR scene;
    scene.width = 16;
    scene.height = 16;
    scene.camera.position = {0.0f, 0.0f, 4.0f};
    scene.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene.camera.fov = 35.0f;
    auto light =
        std::make_shared<ure::scene_ir::MaterialNode>();
    light->model = ure::scene_ir::MaterialModel::Light;
    light->emission = {12.0f, 12.0f, 12.0f};
    scene.materials.push_back(light);
    ure::scene_ir::SphereNode emitter;
    emitter.center = {0.0f, 0.0f, 0.0f};
    emitter.radius = 0.22f;
    emitter.material = light;
    scene.spheres.push_back(emitter);
    return scene;
}

static std::vector<float> render_diffraction_fixture(
    const ure::RenderConfig& config,
    int pass_count) {
    auto engine =
        ure::RenderEngineFactory::create_gpu_renderer(
            config);
    engine->load_scene_ir(make_diffraction_scene());
    for (int pass = 0; pass < pass_count; ++pass) {
        engine->render_pass();
    }
    return engine->get_framebuffer();
}

static float framebuffer_sum(
    const std::vector<float>& framebuffer) {
    float sum = 0.0f;
    for (float value : framebuffer) {
        sum += value;
    }
    return sum;
}

static int test_gpu_diffraction_camera_film_integration() {
    REQUIRE_GPU();
    ure::RenderConfig radiometric;
    radiometric.queue_capacity = 512;
    radiometric.spectral_packet_lanes = 8;
    radiometric.spectral_domain_bins = 8;
    radiometric.max_trace_depth = 2;
    const auto reference =
        render_diffraction_fixture(radiometric, 16);

    auto diffraction = radiometric;
    diffraction.wave_optics.mode =
        ure::WaveOpticsMode::CameraDiffraction;
    diffraction.wave_optics.camera_diffraction_enabled =
        true;
    diffraction.wave_optics.camera_aperture_diameter_m =
        0.2e-3;
    diffraction.wave_optics.camera_focal_length_m =
        50.0e-3;
    diffraction.wave_optics.sensor_pixel_pitch_m =
        4.0e-6;
    diffraction.wave_optics.camera_psf_radius_pixels =
        4;
    diffraction.wave_optics.camera_wavelength_bin_count =
        8;
    diffraction.wave_optics.camera_pupil_sample_count =
        16;
    const auto filtered =
        render_diffraction_fixture(diffraction, 16);
    CHECK(filtered.size() == reference.size());
    const float reference_sum =
        framebuffer_sum(reference);
    const float filtered_sum =
        framebuffer_sum(filtered);
    CHECK(reference_sum > 0.0f);
    CHECK(filtered_sum > 0.0f);
    float difference = 0.0f;
    for (std::size_t index = 0;
         index < reference.size();
         ++index) {
        CHECK(std::isfinite(filtered[index]));
        difference +=
            std::abs(filtered[index] - reference[index]);
    }
    CHECK(difference > 0.01f);
    const float reference_peak =
        *std::max_element(
            reference.begin(),
            reference.end());
    const float filtered_peak =
        *std::max_element(
            filtered.begin(),
            filtered.end());
    CHECK(filtered_peak < reference_peak);
    CHECK_FLOAT_EQ(
        filtered_sum / reference_sum,
        1.0f,
        0.05f);
    return 0;
}

static int test_disabled_diffraction_parameters_are_inert() {
    REQUIRE_GPU();
    ure::RenderConfig first;
    first.queue_capacity = 512;
    first.spectral_packet_lanes = 8;
    first.spectral_domain_bins = 8;
    first.max_trace_depth = 2;
    auto second = first;
    second.wave_optics.camera_aperture_diameter_m =
        0.1e-3;
    second.wave_optics.camera_defocus_waves_at_edge =
        12.0;
    second.wave_optics.camera_aperture_blade_count = 9;
    second.wave_optics.camera_psf_radius_pixels = 17;
    second.wave_optics.camera_wavelength_bin_count = 31;
    const auto a = render_diffraction_fixture(first, 2);
    const auto b = render_diffraction_fixture(second, 2);
    CHECK(a.size() == b.size());
    for (std::size_t index = 0; index < a.size(); ++index) {
        CHECK_FLOAT_EQ(a[index], b[index], 0.0f);
    }
    return 0;
}

static ure::scene_ir::SceneIR make_diffractive_scene(
    ure::scene_ir::DiffractiveOperator diffraction) {
    ure::scene_ir::SceneIR scene;
    scene.width = 16;
    scene.height = 16;
    scene.camera.position = {0.0f, 0.0f, 4.0f};
    scene.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene.camera.fov = 35.0f;
    auto material =
        std::make_shared<
            ure::scene_ir::MaterialNode>();
    material->name = "diffractive";
    material->graph =
        std::make_shared<
            ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode operator_node;
    operator_node.id = 1;
    operator_node.kind =
        static_cast<
            ure::scene_ir::MaterialGraphNodeKind>(
            static_cast<int>(
                ure::scene_ir::MaterialGraphNodeKind::
                    BsdfGrating) +
            static_cast<int>(diffraction.kind));
    operator_node.diffraction =
        std::move(diffraction);
    ure::scene_ir::MaterialGraphNode output;
    output.id = 2;
    output.kind =
        ure::scene_ir::MaterialGraphNodeKind::
            OutputSurface;
    output.inputs.push_back(
        {"surface", operator_node.id, "out"});
    material->graph->nodes = {
        operator_node,
        output};
    material->graph->output_node_id = output.id;
    scene.materials.push_back(material);
    ure::scene_ir::SphereNode sphere;
    sphere.center = {0.0f, 0.0f, 0.0f};
    sphere.radius = 1.2f;
    sphere.material = material;
    scene.spheres.push_back(sphere);
    return scene;
}

static ure::scene_ir::SceneIR make_fluorescent_scene() {
    ure::scene_ir::SceneIR scene;
    scene.width = 16;
    scene.height = 16;
    scene.camera.position = {0.0f, 0.0f, 4.0f};
    scene.camera.look_at = {0.0f, 0.0f, 0.0f};
    scene.camera.fov = 35.0f;
    auto material =
        std::make_shared<
            ure::scene_ir::MaterialNode>();
    material->name = "fluorescent";
    material->graph =
        std::make_shared<
            ure::scene_ir::MaterialGraph>();
    ure::scene_ir::MaterialGraphNode fluorescence;
    fluorescence.id = 1;
    fluorescence.kind =
        ure::scene_ir::MaterialGraphNodeKind::
            BsdfFluorescence;
    fluorescence.fluorescence.resource_id =
        "gpu/fluorescence";
    fluorescence.fluorescence
        .excitation_wavelengths_nm = {
            400.0f,
            500.0f};
    fluorescence.fluorescence
        .emission_wavelengths_nm = {
            600.0f,
            700.0f};
    fluorescence.fluorescence
        .excitation_efficiency = {
            0.8f,
            0.6f};
    fluorescence.fluorescence.quantum_yield = {
        0.75f,
        0.5f};
    fluorescence.fluorescence
        .emission_pdf_per_nm = {
            0.01f,
            0.01f,
            0.01f,
            0.01f};
    fluorescence.fluorescence.lifetime_seconds =
        0.002;
    ure::scene_ir::MaterialGraphNode output;
    output.id = 2;
    output.kind =
        ure::scene_ir::MaterialGraphNodeKind::
            OutputSurface;
    output.inputs.push_back(
        {"surface", fluorescence.id, "out"});
    material->graph->nodes = {
        fluorescence,
        output};
    material->graph->output_node_id = output.id;
    scene.materials.push_back(material);
    ure::scene_ir::SphereNode sphere;
    sphere.center = {0.0f, 0.0f, 0.0f};
    sphere.radius = 1.2f;
    sphere.material = material;
    scene.spheres.push_back(sphere);
    return scene;
}

static std::vector<float> render_diffractive_scene(
    const ure::scene_ir::SceneIR& scene,
    int pass_count) {
    ure::RenderConfig config;
    config.queue_capacity = 8192;
    config.spectral_packet_lanes = 8;
    config.spectral_domain_bins = 8;
    config.max_trace_depth = 2;
    config.wave_optics.diffractive_materials_enabled =
        true;
    auto engine =
        ure::RenderEngineFactory::create_gpu_renderer(
            config);
    engine->load_scene_ir(scene);
    for (int pass = 0; pass < pass_count; ++pass) {
        engine->render_pass();
    }
    return engine->get_framebuffer();
}

static float center_framebuffer_sum(
    const std::vector<float>& framebuffer) {
    float sum = 0.0f;
    for (int y = 4; y < 12; ++y) {
        for (int x = 4; x < 12; ++x) {
            const std::size_t base =
                static_cast<std::size_t>(
                    (y * 16 + x) * 3);
            sum += framebuffer[base] +
                   framebuffer[base + 1] +
                   framebuffer[base + 2];
        }
    }
    return sum;
}

static ure::wave::LocalFullWaveArtifact
make_verified_gpu_fullwave_artifact() {
    ure::wave::LocalFullWaveRequest request;
    request.request_id = "gpu/wave/cell";
    request.provider_id = "ure.test.gpu-fullwave";
    request.minimum_provider_version = {1, 0, 0};
    request.solver_kind =
        ure::wave::LocalFullWaveSolverKind::Rcwa;
    request.geometry_payload = {1, 2, 3, 4};
    request.material_payload = {5, 6, 7, 8};
    request.geometry_digest =
        "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a";
    request.material_digest =
        "55e5509f8052998294266ee5b50cb592938191fb5d67f73cac2e60b0276b1bdd";
    request.wavelengths_nm = {400.0f, 800.0f};
    request.incident_cosines = {1.0f};
    request.minimum_order = 0;
    request.maximum_order = 0;
    request.reflection = true;
    request.transmission = false;
    request.period_m = 1.0e-6;
    request.memory_budget_bytes = 1024 * 1024;
    request.iteration_budget = 1024;

    ure::wave::LocalFullWaveProviderDescriptor
        descriptor;
    descriptor.provider_id = request.provider_id;
    descriptor.version = {1, 0, 0};
    descriptor.executable_digest =
        "ba7c83bf49d98d20e7762a7eb9cc5796893f8a0b08bb1eff0b23f848592de60e";
    descriptor.semantic_digest =
        "52ce5029c72446ac4f684946be0f65e275af436578108ae3cd17f630c1c5b243";
    descriptor.solver_kinds = {
        ure::wave::LocalFullWaveSolverKind::Rcwa};
    descriptor.maximum_wavelength_samples = 2;
    descriptor.maximum_incidence_samples = 1;
    descriptor.maximum_scattering_entries = 2;
    descriptor.maximum_memory_bytes =
        request.memory_budget_bytes;
    descriptor.deterministic = true;

    ure::scene_ir::DiffractiveOperator table;
    table.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            ScatteringTable;
    table.period_m = request.period_m;
    table.max_order = 0;
    table.table_id = "gpu/rcwa";
    for (const float wavelength :
         request.wavelengths_nm) {
        ure::scene_ir::DiffractiveScatteringEntry
            entry;
        entry.wavelength_nm = wavelength;
        entry.incident_cosine = 1.0f;
        entry.jones_ss.real =
            wavelength == 400.0f ? 0.4f : 0.7f;
        entry.jones_pp.real =
            entry.jones_ss.real;
        table.table.push_back(entry);
    }

    ure::wave::LocalFullWaveEvidence evidence;
    evidence.converged = true;
    evidence.iterations = 32;
    evidence.peak_memory_bytes = 4096;
    evidence.residual = 1.0e-7;
    evidence.reciprocity_error = 1.0e-6;
    evidence.energy_error = 1.0e-6;
    evidence.solver_artifact_digest =
        "7280ce41975543e7cda0ca68894eca8d1c5faa51bda65e5cd0f0963db3abbc05";
    return ure::wave::make_local_fullwave_artifact(
        request,
        descriptor,
        std::move(table),
        std::move(evidence));
}

static int test_gpu_diffractive_material_transport() {
    REQUIRE_GPU();
    ure::scene_ir::DiffractiveOperator phase;
    phase.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            PhaseMask;
    phase.phase_depth_rad = 0.0;
    phase.max_order = 3;
    const auto phase_frame =
        render_diffractive_scene(
            make_diffractive_scene(phase),
            24);

    ure::scene_ir::DiffractiveOperator grating;
    grating.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            Grating;
    grating.period_m = 1.1e-6;
    grating.duty_cycle = 0.4;
    grating.max_order = 3;
    const auto grating_frame =
        render_diffractive_scene(
            make_diffractive_scene(grating),
            24);
    grating.orientation_rad = 0.5 *
        3.14159265358979323846;
    const auto rotated_frame =
        render_diffractive_scene(
            make_diffractive_scene(grating),
            24);

    const auto fullwave_artifact =
        make_verified_gpu_fullwave_artifact();
    CHECK(
        fullwave_artifact.schema_identity ==
        "ure.local-fullwave.scattering/1.0");
    CHECK(fullwave_artifact.scattering.table.size() ==
          2);
    const auto table_frame =
        render_diffractive_scene(
            make_diffractive_scene(
                fullwave_artifact.scattering),
            24);

    CHECK(phase_frame.size() == 16 * 16 * 3);
    CHECK(grating_frame.size() == phase_frame.size());
    CHECK(rotated_frame.size() == phase_frame.size());
    CHECK(table_frame.size() == phase_frame.size());
    float orientation_difference = 0.0f;
    for (std::size_t index = 0;
         index < phase_frame.size();
         ++index) {
        CHECK(std::isfinite(phase_frame[index]));
        CHECK(std::isfinite(grating_frame[index]));
        CHECK(std::isfinite(rotated_frame[index]));
        CHECK(std::isfinite(table_frame[index]));
        orientation_difference +=
            std::abs(
                grating_frame[index] -
                rotated_frame[index]);
    }
    const float phase_energy =
        center_framebuffer_sum(phase_frame);
    const float grating_energy =
        center_framebuffer_sum(grating_frame);
    const float table_energy =
        center_framebuffer_sum(table_frame);
    CHECK(phase_energy > 0.0f);
    CHECK(grating_energy > 0.0f);
    CHECK(table_energy > 0.0f);
    CHECK(grating_energy < phase_energy * 0.75f);
    CHECK(table_energy < phase_energy * 0.75f);
    CHECK(orientation_difference > 0.01f);
    return 0;
}

static int test_gpu_diffractive_material_requires_gate() {
    REQUIRE_GPU();
    ure::scene_ir::DiffractiveOperator grating;
    grating.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            Grating;
    auto engine =
        ure::RenderEngineFactory::create_gpu_renderer(
            ure::RenderConfig{});
    bool rejected = false;
    try {
        engine->load_scene_ir(
            make_diffractive_scene(grating));
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()).find(
                "diffractive") !=
            std::string::npos;
    }
    CHECK(rejected);
    return 0;
}

static int test_gpu_diffractive_material_update_requires_reload() {
    REQUIRE_GPU();
    ure::scene_ir::DiffractiveOperator grating;
    grating.kind =
        ure::scene_ir::DiffractiveOperatorKind::
            Grating;
    ure::RenderConfig config;
    config.queue_capacity = 512;
    config.spectral_packet_lanes = 8;
    config.spectral_domain_bins = 8;
    config.wave_optics.diffractive_materials_enabled =
        true;
    auto compiled = ure::GpuSceneCompiler::compile(
        make_diffractive_scene(grating),
        config);
    int material_index = -1;
    for (std::size_t index = 0;
         index < compiled.materials.size();
         ++index) {
        if (compiled.materials[index].header.type ==
            ure::gpu::MaterialType::Diffractive) {
            material_index =
                static_cast<int>(index);
            break;
        }
    }
    CHECK(material_index >= 0);
    ure::gpu::GpuContext* context =
        ure::gpu::init_gpu_renderer(
            compiled.width,
            compiled.height,
            compiled.meshes,
            compiled.instances,
            compiled.spheres,
            compiled.materials,
            compiled.textures,
            config);
    CHECK(context != nullptr);
    material_index = -1;
    for (std::size_t index = 0;
         index <
             context->
                 host_materials_for_light_distribution
                     .size();
         ++index) {
        if (context->
                host_materials_for_light_distribution[
                    index]
                    .header.type ==
            ure::gpu::MaterialType::Diffractive) {
            material_index =
                static_cast<int>(index);
            break;
        }
    }
    CHECK(material_index >= 0);
    ure::gpu::GpuMaterialData ordinary =
        context->host_materials_for_light_distribution[
            static_cast<std::size_t>(material_index)];
    ordinary.header.type =
        ure::gpu::MaterialType::Lambertian;
    ordinary.diffraction_table.clear();
    bool rejected = false;
    try {
        ure::gpu::update_materials_gpu(
            context,
            &ordinary,
            1,
            material_index);
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()).find(
                "full scene reload") !=
            std::string::npos;
    }
    CHECK(rejected);
    ure::gpu::free_gpu_renderer(context);
    return 0;
}

static int test_gpu_fluorescence_transport_and_gate() {
    REQUIRE_GPU();
    auto disabled =
        ure::RenderEngineFactory::create_gpu_renderer(
            ure::RenderConfig{});
    bool rejected = false;
    try {
        disabled->load_scene_ir(
            make_fluorescent_scene());
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()).find(
                "fluorescence") !=
            std::string::npos;
    }
    CHECK(rejected);

    ure::RenderConfig config;
    config.queue_capacity = 8192;
    config.spectral_packet_lanes = 8;
    config.spectral_domain_bins = 8;
    config.max_trace_depth = 3;
    config.wave_optics.fluorescence_enabled = true;
    auto engine =
        ure::RenderEngineFactory::create_gpu_renderer(
            config);
    engine->load_scene_ir(make_fluorescent_scene());
    for (int pass = 0; pass < 32; ++pass) {
        engine->render_pass();
    }
    const auto framebuffer = engine->get_framebuffer();
    CHECK(framebuffer.size() == 16 * 16 * 3);
    float center_energy = 0.0f;
    float center_red = 0.0f;
    float center_blue = 0.0f;
    for (int y = 4; y < 12; ++y) {
        for (int x = 4; x < 12; ++x) {
            const std::size_t base =
                static_cast<std::size_t>(
                    (y * 16 + x) * 3);
            CHECK(std::isfinite(framebuffer[base]));
            CHECK(std::isfinite(framebuffer[base + 1]));
            CHECK(std::isfinite(framebuffer[base + 2]));
            center_red += framebuffer[base];
            center_energy +=
                framebuffer[base] +
                framebuffer[base + 1] +
                framebuffer[base + 2];
            center_blue += framebuffer[base + 2];
        }
    }
    CHECK(center_energy > 0.0f);
    CHECK(center_red > center_blue);
    return 0;
}

static int test_gpu_fluorescence_update_requires_reload() {
    REQUIRE_GPU();
    ure::RenderConfig config;
    config.queue_capacity = 512;
    config.spectral_packet_lanes = 8;
    config.spectral_domain_bins = 8;
    config.wave_optics.fluorescence_enabled = true;
    auto compiled = ure::GpuSceneCompiler::compile(
        make_fluorescent_scene(),
        config);
    auto constrained = config;
    constrained.backend.memory_budget_bytes = 1024;
    bool budget_rejected = false;
    try {
        ure::gpu::GpuContext* unexpected =
            ure::gpu::init_gpu_renderer(
                compiled.width,
                compiled.height,
                compiled.meshes,
                compiled.instances,
                compiled.spheres,
                compiled.materials,
                compiled.textures,
                constrained);
        ure::gpu::free_gpu_renderer(unexpected);
    } catch (const std::runtime_error& error) {
        budget_rejected =
            std::string(error.what()).find(
                "fluorescence queue state") !=
            std::string::npos;
    }
    CHECK(budget_rejected);
    ure::gpu::GpuContext* context =
        ure::gpu::init_gpu_renderer(
            compiled.width,
            compiled.height,
            compiled.meshes,
            compiled.instances,
            compiled.spheres,
            compiled.materials,
            compiled.textures,
            config);
    CHECK(context != nullptr);
    int material_index = -1;
    for (std::size_t index = 0;
         index <
             context->
                 host_materials_for_light_distribution
                     .size();
         ++index) {
        if (context->
                host_materials_for_light_distribution[
                    index]
                    .header.type ==
            ure::gpu::MaterialType::Fluorescent) {
            material_index =
                static_cast<int>(index);
            break;
        }
    }
    CHECK(material_index >= 0);
    ure::gpu::GpuMaterialData ordinary =
        context->host_materials_for_light_distribution[
            static_cast<std::size_t>(material_index)];
    ordinary.header.type =
        ure::gpu::MaterialType::Lambertian;
    ordinary.fluorescence = {};
    bool rejected = false;
    try {
        ure::gpu::update_materials_gpu(
            context,
            &ordinary,
            1,
            material_index);
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()).find(
                "full scene reload") !=
            std::string::npos;
    }
    CHECK(rejected);
    ure::gpu::free_gpu_renderer(context);
    return 0;
}

static int test_gpu_partial_coherence_ensemble_reduction() {
    REQUIRE_GPU();
    const std::vector<ure::wave::WavePoint2D>
        points{
            {-0.75e-3, 0.0},
            {-0.25e-3, 0.0},
            {0.25e-3, 0.0},
            {0.75e-3, 0.0}};
    const auto target =
        ure::wave::make_gaussian_schell_csd(
            632.8e-9,
            points,
            2.0e-3,
            0.6e-3,
            1.0);
    std::vector<ure::wave::CoherentRealization>
        realizations;
    realizations.reserve(2048);
    for (std::uint64_t id = 0;
         id < 2048;
         ++id) {
        auto realization =
            ure::wave::sample_coherent_realization(
                target,
                id);
        realization.statistical_weight =
            id % 3 == 0 ? 2.0 : 1.0;
        realizations.push_back(
            std::move(realization));
    }
    const auto host =
        ure::wave::estimate_cross_spectral_density(
            target.wavelength_m,
            points,
            realizations);
    const auto device =
        ure::wave::
            estimate_cross_spectral_density_gpu(
                target.wavelength_m,
                points,
                realizations);
    CHECK(host.is_valid(1.0e-8));
    CHECK(device.is_valid(1.0e-7));
    CHECK(host.values.size() ==
          device.values.size());
    for (std::size_t index = 0;
         index < host.values.size();
         ++index) {
        CHECK_FLOAT_EQ(
            static_cast<float>(
                device.values[index].real),
            static_cast<float>(
                host.values[index].real),
            1.0e-5f);
        CHECK_FLOAT_EQ(
            static_cast<float>(
                device.values[index].imag),
            static_cast<float>(
                host.values[index].imag),
            1.0e-5f);
    }
    auto invalid = realizations;
    invalid[0].statistical_weight = 0.0;
    CHECK(ure::wave::
              estimate_cross_spectral_density_gpu(
                  target.wavelength_m,
                  points,
                  invalid)
              .values.empty());
    return 0;
}

static int test_gpu_anisotropic_modal_transport() {
    REQUIRE_GPU();
    const auto medium =
        ure::wave::make_anisotropic_medium({
            ure::wave::make_uniaxial_sample(
                450.0e-9,
                1.53,
                1.64,
                {1.0, 0.0, 0.0},
                0.002,
                0.01,
                0.2),
            ure::wave::make_uniaxial_sample(
                550.0e-9,
                1.51,
                1.61,
                {1.0, 0.0, 0.0},
                0.001,
                0.008,
                0.15),
            ure::wave::make_uniaxial_sample(
                650.0e-9,
                1.49,
                1.58,
                {1.0, 0.0, 0.0},
                0.0005,
                0.006,
                0.1)});
    CHECK(medium.is_valid());
    std::vector<ure::wave::ModalPropagationSample>
        samples(4);
    samples[0] = {
        {0.0, 0.0, 1.0},
        450.0e-9,
        2.0e-6,
        {{1.0, 0.0}, {0.0, 0.0}}};
    samples[1] = {
        {0.2, 0.1, 1.0},
        500.0e-9,
        4.0e-6,
        {{0.5, 0.25}, {-0.1, 0.7}}};
    samples[2] = {
        {-0.3, 0.4, 1.0},
        550.0e-9,
        7.0e-6,
        {{0.0, 1.0}, {1.0, 0.0}}};
    samples[3] = {
        {0.0, 1.0, 0.25},
        650.0e-9,
        1.0e-5,
        {{-0.3, 0.2}, {0.8, -0.1}}};
    const auto device =
        ure::wave::
            propagate_anisotropic_displacements_gpu(
                medium,
                samples);
    CHECK(device.size() == samples.size());
    for (std::size_t index = 0;
         index < samples.size();
         ++index) {
        const auto host =
            ure::wave::
                propagate_anisotropic_displacement(
                medium,
                samples[index]);
        CHECK(host.is_valid());
        CHECK(
            host.transverse_displacement.power() <=
            samples[index].
                    transverse_displacement.power() +
                1.0e-10);
        CHECK(std::abs(
                  device[index].x.real -
                  host.transverse_displacement.x.real) <=
              1.0e-9);
        CHECK(std::abs(
                  device[index].x.imag -
                  host.transverse_displacement.x.imag) <=
              1.0e-9);
        CHECK(std::abs(
                  device[index].y.real -
                  host.transverse_displacement.y.real) <=
              1.0e-9);
        CHECK(std::abs(
                  device[index].y.imag -
                  host.transverse_displacement.y.imag) <=
              1.0e-9);
    }
    auto invalid = samples;
    invalid[0].wavelength_m = 700.0e-9;
    CHECK(ure::wave::
              propagate_anisotropic_displacements_gpu(
                  medium,
                  invalid)
              .empty());
    return 0;
}

int main() {
    std::printf("[GPU Wave Optics Test]\n");
    RUN_TEST(test_gpu_diffractive_jones_response);
    RUN_TEST(test_gpu_fraunhofer_uniform_field);
    RUN_TEST(test_gpu_fraunhofer_matches_cpu_reference);
    RUN_TEST(test_gpu_fraunhofer_invalid_fails_closed);
    RUN_TEST(test_gpu_diffraction_camera_film_integration);
    RUN_TEST(test_disabled_diffraction_parameters_are_inert);
    RUN_TEST(test_gpu_diffractive_material_transport);
    RUN_TEST(test_gpu_diffractive_material_requires_gate);
    RUN_TEST(test_gpu_diffractive_material_update_requires_reload);
    RUN_TEST(test_gpu_fluorescence_transport_and_gate);
    RUN_TEST(test_gpu_fluorescence_update_requires_reload);
    RUN_TEST(test_gpu_partial_coherence_ensemble_reduction);
    RUN_TEST(test_gpu_anisotropic_modal_transport);
    std::printf("  passed: %d, failed: %d\n", g_tests_passed, g_tests_failed);
    return g_test_result;
}
