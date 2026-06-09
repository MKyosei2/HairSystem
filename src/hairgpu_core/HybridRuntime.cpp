// Clean ordered aggregation unit generated from the original monolithic source.
// Keep this file as the only compiled translation unit; edit ordered .inl parts below.
// See README_SOURCE_ORDER.md in the part directory for the reading order.

#include "hairgpu/HybridRuntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

#include "hairgpu/AssetIO.h"
#include "hairgpu/GuideCache.h"
#include "hairgpu/HairCache.h"
#include "hairgpu/Metrics.h"
#include "hairgpu/ReducedModel.h"
#include "hairgpu/Geometry.h"
#include "hairgpu/SdfGridUtil.h"

#include "hairgpu/ErrorMetrics.h"

#include "HybridRuntime_parts/00_helpers_error_metrics_and_frame_asset.inl"  // helpers_error_metrics_and_frame_asset
#include "HybridRuntime_parts/01_guide_lod_and_cache_runtime.inl"  // guide_lod_and_cache_runtime
#include "HybridRuntime_parts/02_hybrid_reduction_and_error_reports.inl"  // hybrid_reduction_and_error_reports
#include "HybridRuntime_parts/03_sparse_corrective_and_active_runtime.inl"  // sparse_corrective_and_active_runtime
#include "HybridRuntime_parts/04_temporal_runtime_and_summary.inl"  // temporal_runtime_and_summary
#include "HybridRuntime_parts/05_part_05.inl"  // part_05
#include "HybridRuntime_parts/06_part_06.inl"  // part_06
