#include <cstdio>
#include <type_traits>

#include "ure/backend.hpp"
#include "ure/gpu_auto_config.hpp"
#include "ure/gpu_hardware.hpp"
#include "ure/render.hpp"
#include "ure/session.hpp"
#include "ure/ure_c_api.h"
#include "ure/wave_optics.hpp"
#include "ure/anisotropic_optics.hpp"
#include "ure/local_fullwave.hpp"
#include "ure/distributed_wave_io.hpp"
#include "ure/usd_schema_adapter.hpp"

int main() {
    static_assert(std::is_enum_v<ure::BackendKind>);
    static_assert(
        ure::usd::kUsdSchemaAdapterVersion.major == 1);
    static_assert(std::is_abstract_v<ure::IRenderEngine>);
    static_assert(std::is_standard_layout_v<ure_backend_config_t>);
    std::printf("SDK-free public surface compiled\n");
    return 0;
}
