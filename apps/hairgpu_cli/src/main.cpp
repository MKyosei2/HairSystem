// Clean ordered aggregation unit generated from the original monolithic source.
// Keep this file as the only compiled translation unit; edit ordered .inl parts below.
// See README_SOURCE_ORDER.md in the part directory for the reading order.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "hairgpu/ActiveSet.h"
#include "hairgpu/AssetIO.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/Binding.h"
#include "hairgpu/BoneAnimation.h"
#include "hairgpu/Broadphase.h"
#include "hairgpu/Cards.h"
#include "hairgpu/Collision.h"
#include "hairgpu/DccInterop.h"
#include "hairgpu/GuideCache.h"
#include "hairgpu/HybridRuntime.h"
#include "hairgpu/HairCache.h"
#include "hairgpu/Lod.h"
#include "hairgpu/MeshIO.h"
#include "hairgpu/Metrics.h"
#include "hairgpu/Pipeline.h"
#include "hairgpu/ReducedModel.h"
#include "hairgpu/UsdExport.h"
#include "hairgpu/Validation.h"

#include "main_parts/00_helpers_and_asset_io_commands.inl"  // helpers_and_asset_io_commands
#include "main_parts/01_runtime_suite_and_evaluation_commands.inl"  // runtime_suite_and_evaluation_commands
#include "main_parts/02_authoring_edit_metrics_and_reports.inl"  // authoring_edit_metrics_and_reports
#include "main_parts/03_main_argument_dispatch.inl"  // main_argument_dispatch
#include "main_parts/04_part_04.inl"  // part_04
#include "main_parts/05_part_05.inl"  // part_05
#include "main_parts/06_part_06.inl"  // part_06
#include "main_parts/07_part_07.inl"  // part_07
