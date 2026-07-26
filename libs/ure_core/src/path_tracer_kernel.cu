#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include "ure/detail/cuda_structs.cuh"
#include "ure/detail/cuda_texture_view.cuh"
#include "ure/gpu_spectrum_utils.cuh"
#include "ure/gpu_material_helpers.cuh"
#include "ure/path_tracer_sampling.cuh"
#include "ure/integrator/restir_pt.cuh"
#include "ure/integrator/bidirectional.cuh"

namespace ure::gpu {

#include "path_tracer_decl.cuh"
#include "path_tracer_intersect.cuh"
#include "path_tracer_polarization.cuh"
#include "path_tracer_boundary.cuh"
#include "path_tracer_bsdf.cuh"
#include "path_tracer_scattered_stokes.cuh"
#include "path_tracer_volume.cuh"
#include "restir_pt_capture.cuh"
#include "path_tracer_wavefront.cuh"

#include "path_tracer_material.cu"

} // namespace ure::gpu
