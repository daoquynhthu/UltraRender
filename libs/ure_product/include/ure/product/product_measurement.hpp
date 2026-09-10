#pragma once

#include <ure/product/product_service.hpp>
#include <ure/render.hpp>

#include <span>

namespace ure::product {

ProductMeasurementSet make_product_measurement_set(
    const RenderMeasurementStatistics& statistics,
    const ProductIdentitySet& identities,
    std::uint64_t accepted_samples,
    const Identity& producer_identity);

ProductMeasurementSet merge_product_measurement_sets(
    std::span<const ProductMeasurementSet> sets);

}
