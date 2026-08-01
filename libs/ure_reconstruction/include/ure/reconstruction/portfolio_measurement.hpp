#pragma once

#include "ure/reconstruction/measurement.hpp"
#include "ure/transport/portfolio.hpp"

#include <cstddef>

namespace ure::reconstruction {

MeasurementProvenance make_portfolio_measurement_provenance(
    const transport::PortfolioSchedule& schedule,
    const transport::PortfolioScheduleShard& shard,
    std::size_t slice_ordinal,
    semantic::ProvenanceIdentitySet identities);

}
