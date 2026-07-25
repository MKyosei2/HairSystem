// main.cpp ordered part 01: runtime_suite_and_evaluation_commands
// This file is included by ../main.cpp; do not compile it directly.

        outError.clear();
        return true;
    }
    bool WriteUsdSequenceManifest(const std::filesystem::path& outputDirectory,
                                  const char* sequenceName,
                                  uint32_t frameCount,
                                  float frameRate,
                                  uint32_t strandCount,
                                  bool guidesOnly,
                                  uint32_t everyNth,
                                  std::string& outError)
    {
        std::ofstream out(outputDirectory / "sequence_manifest.json");
        if (!out)
        {
            outError = "Failed to write USDA sequence manifest.";
            return false;
        }
        out << "{\n";
        out << "  \"sequence\": \"" << sequenceName << "\",\n";
        out << "  \"frameCount\": " << frameCount << ",\n";
        out << "  \"frameRate\": " << frameRate << ",\n";
        out << "  \"strandCount\": " << strandCount << ",\n";
        out << "  \"guidesOnly\": " << (guidesOnly ? "true" : "false") << ",\n";
        out << "  \"everyNth\": " << everyNth << ",\n";
        out << "  \"pattern\": \"frame_%04d.usda\"\n";
        out << "}\n";
        outError.clear();
        return true;
    }
    bool TryParseBoolToken(const std::string& text, bool& outValue)
    {
        const std::string lowered = ToLower(Trim(text));
        if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
        {
            outValue = true;
            return true;
        }
        if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
        {
            outValue = false;
            return true;
        }
        return false;
    }

    std::string JsonEscape(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8u);
        for (char c : value)
        {
            switch (c)
            {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(c); break;
            }
        }
        return escaped;
    }

    bool ParseRegionModeToken(const std::string& text, RegionSelectMode& outMode)
    {
        const std::string lowered = ToLower(Trim(text));
        if (lowered.empty() || lowered == "all")
        {
            outMode = RegionSelectMode::All;
            return true;
        }
        if (lowered == "ellipse" || lowered == "circle")
        {
            outMode = RegionSelectMode::Ellipse;
            return true;
        }
        if (lowered == "rectangle" || lowered == "rect" || lowered == "box")
        {
            outMode = RegionSelectMode::Rectangle;
            return true;
        }
        if (lowered == "sector" || lowered == "fan")
        {
            outMode = RegionSelectMode::Sector;
            return true;
        }
        return false;
    }

    const char* RegionModeToString(RegionSelectMode mode)
    {
        switch (mode)
        {
        case RegionSelectMode::Ellipse: return "ellipse";
        case RegionSelectMode::Rectangle: return "rectangle";
        case RegionSelectMode::Sector: return "sector";
        default: return "all";
        }
    }

    bool ParseStrandSelectionModeToken(const std::string& text, StrandSelectionMode& outMode)
    {
        const std::string lowered = ToLower(Trim(text));
        if (lowered.empty() || lowered == "none")
        {
            outMode = StrandSelectionMode::None;
            return true;
        }
        if (lowered == "union")
        {
            outMode = StrandSelectionMode::Union;
            return true;
        }
        if (lowered == "replace" || lowered == "exact")
        {
            outMode = StrandSelectionMode::Replace;
            return true;
        }
        if (lowered == "guide_family" || lowered == "guidefamily" || lowered == "family")
        {
            outMode = StrandSelectionMode::GuideFamily;
            return true;
        }
        return false;
    }

    const char* StrandSelectionModeToString(StrandSelectionMode mode)
    {
        switch (mode)
        {
        case StrandSelectionMode::Union: return "union";
        case StrandSelectionMode::Replace: return "replace";
        case StrandSelectionMode::GuideFamily: return "guide_family";
        default: return "none";
        }
    }

    bool LocalLoadEditStackCsv(const std::filesystem::path& path, EditStack& outStack, std::string& outError)
    {
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open edit stack CSV: " + path.string();
            return false;
        }

        outStack = {};
        std::string line;
        uint32_t lineNumber = 0u;
        while (std::getline(in, line))
        {
            ++lineNumber;
            line = Trim(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::vector<std::string> tokens = SplitSimpleCsv(line);
            for (std::string& token : tokens)
            {
                token = Trim(token);
            }
            if (tokens.empty())
            {
                continue;
            }

            const std::string kind = ToLower(tokens[0]);
            if (kind == "stack")
            {
                if (tokens.size() >= 2u && !tokens[1].empty() && !TryParseFloatToken(tokens[1], outStack.boundarySmoothing))
                {
                    outError = "Malformed boundary smoothing at line " + std::to_string(lineNumber);
                    return false;
                }
                if (tokens.size() >= 3u && !tokens[2].empty() && !TryParseFloatToken(tokens[2], outStack.boundaryRadiusUv))
                {
                    outError = "Malformed boundary radius at line " + std::to_string(lineNumber);
                    return false;
                }
                continue;
            }

            if (kind != "layer")
            {
                outError = "Unsupported edit stack row at line " + std::to_string(lineNumber) + ": " + line;
                return false;
            }

            EditLayer layer = {};
            layer.name = (tokens.size() >= 2u && !tokens[1].empty()) ? tokens[1] : ("layer_" + std::to_string(outStack.layers.size()));
            bool enabled = true;
            if (tokens.size() >= 3u && !tokens[2].empty() && !TryParseBoolToken(tokens[2], enabled))
            {
                outError = "Malformed enabled flag at line " + std::to_string(lineNumber);
                return false;
            }
            layer.enabled = enabled;
            if (tokens.size() >= 4u && !tokens[3].empty() && !TryParseFloatToken(tokens[3], layer.layerWeight))
            {
                outError = "Malformed layer weight at line " + std::to_string(lineNumber);
                return false;
            }
            if (tokens.size() >= 5u && !tokens[4].empty() && !ParseRegionModeToken(tokens[4], layer.region.mode))
            {
                outError = "Malformed region mode at line " + std::to_string(lineNumber);
                return false;
            }

            auto parseFloatOptional = [&](size_t index, float& value, const char* label) -> bool
            {
                if (index >= tokens.size() || tokens[index].empty())
                {
                    return true;
                }
                if (!TryParseFloatToken(tokens[index], value))
                {
                    outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                    return false;
                }
                return true;
            };
            auto parseUIntOptional = [&](size_t index, uint32_t& value, const char* label) -> bool
            {
                if (index >= tokens.size() || tokens[index].empty())
                {
                    return true;
                }
                if (!TryParseUIntToken(tokens[index], value))
                {
                    outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                    return false;
                }
                return true;
            };
            auto parseBoolOptional = [&](size_t index, bool& value, const char* label) -> bool
            {
                if (index >= tokens.size() || tokens[index].empty())
                {
                    return true;
                }
                if (!TryParseBoolToken(tokens[index], value))
                {
                    outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                    return false;
                }
                return true;
            };

            uint32_t groupId = UINT32_MAX;
            if (!parseFloatOptional(5u, layer.region.centerUv.x, "center u") ||
                !parseFloatOptional(6u, layer.region.centerUv.y, "center v") ||
                !parseFloatOptional(7u, layer.region.radiusUv.x, "radius x") ||
                !parseFloatOptional(8u, layer.region.radiusUv.y, "radius y") ||
                !parseFloatOptional(9u, layer.region.innerRadius, "inner radius") ||
                !parseFloatOptional(10u, layer.region.angleCenterRadians, "angle center") ||
                !parseFloatOptional(11u, layer.region.angleHalfExtentRadians, "angle half extent") ||
                !parseFloatOptional(12u, layer.region.feather, "feather") ||
                !parseUIntOptional(13u, groupId, "groupId") ||
                !parseBoolOptional(14u, layer.region.guidesOnly, "guidesOnly") ||
                !parseFloatOptional(15u, layer.edit.lengthScale, "lengthScale") ||
                !parseFloatOptional(16u, layer.edit.widthScale, "widthScale") ||
                !parseFloatOptional(17u, layer.edit.rootOffset.x, "rootOffsetX") ||
                !parseFloatOptional(18u, layer.edit.rootOffset.y, "rootOffsetY") ||
                !parseFloatOptional(19u, layer.edit.rootOffset.z, "rootOffsetZ") ||
                !parseFloatOptional(20u, layer.edit.tipOffset.x, "tipOffsetX") ||
                !parseFloatOptional(21u, layer.edit.tipOffset.y, "tipOffsetY") ||
                !parseFloatOptional(22u, layer.edit.tipOffset.z, "tipOffsetZ") ||
                !parseFloatOptional(23u, layer.edit.sideOffset, "sideOffset") ||
                !parseFloatOptional(24u, layer.edit.yawRadians, "yawRadians") ||
                !parseFloatOptional(25u, layer.edit.followerInfluence, "followerInfluence") ||
                !parseFloatOptional(26u, layer.edit.clumpStrength, "clumpStrength") ||
                !parseFloatOptional(27u, layer.edit.curlStrength, "curlStrength") ||
                !parseFloatOptional(28u, layer.edit.partingStrength, "partingStrength") ||
                !parseFloatOptional(29u, layer.edit.partingCenterU, "partingCenterU") ||
                !parseFloatOptional(30u, layer.edit.partingWidthUv, "partingWidthUv"))
            {
                return false;
            }
            layer.region.groupId = groupId;
            const bool hasExtendedEditColumns = tokens.size() >= 34u;
            const size_t strandSelectionColumn = hasExtendedEditColumns ? 31u : 26u;
            const size_t strandSelectionModeColumn = hasExtendedEditColumns ? 32u : 27u;
            const size_t strandSelectionWeightColumn = hasExtendedEditColumns ? 33u : 28u;
            if (tokens.size() > strandSelectionColumn && !tokens[strandSelectionColumn].empty())
            {
                if (!ParseStrandSelectionSpec(tokens[strandSelectionColumn], layer.strandSelection, outError))
                {
                    outError += " at line " + std::to_string(lineNumber);
                    return false;
                }
            }
            if (tokens.size() > strandSelectionModeColumn && !tokens[strandSelectionModeColumn].empty() && !ParseStrandSelectionModeToken(tokens[strandSelectionModeColumn], layer.strandSelectionMode))
            {
                outError = "Malformed strand selection mode at line " + std::to_string(lineNumber);
                return false;
            }
            if (tokens.size() > strandSelectionWeightColumn && !tokens[strandSelectionWeightColumn].empty() && !TryParseFloatToken(tokens[strandSelectionWeightColumn], layer.strandSelectionWeight))
            {
                outError = "Malformed strand selection weight at line " + std::to_string(lineNumber);
                return false;
            }
            if (!layer.strandSelection.empty() && layer.strandSelectionMode == StrandSelectionMode::None)
            {
                layer.strandSelectionMode = StrandSelectionMode::Union;
            }
            outStack.layers.push_back(layer);
        }

        outError.clear();
        return true;
    }

    bool LocalWriteEditStackTemplateCsv(const std::filesystem::path& path, std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to open edit stack template for writing: " + path.string();
            return false;
        }
        out << "# edit stack csv\n";
        out << "# stack,boundarySmoothing,boundaryRadiusUv\n";
        out << "stack,0.42,0.10\n";
        out << "# layer,name,enabled,layerWeight,mode,cx,cy,rx,ry,innerRadius,angleCenterRadians,angleHalfExtentRadians,feather,groupId,guidesOnly,lengthScale,widthScale,rootOffsetX,rootOffsetY,rootOffsetZ,tipOffsetX,tipOffsetY,tipOffsetZ,sideOffset,yawRadians,followerInfluence,clumpStrength,curlStrength,partingStrength,partingCenterU,partingWidthUv,zone,semantic,flowAlignStrength,smoothStrength,liftStrength,pressStrength,twistStrength,strandSelection,strandSelectionMode,strandSelectionWeight\n";
        out << "layer,front_sector,1,1.0,sector,0.50,0.50,0.50,0.50,0.00,0.0000000,3.1415926,0.20,,1,1.05,1.00,0.00,-0.01,0.00,0.00,-0.03,0.00,0.00,0.05,1.00,0.18,0.00,0.012,0.50,0.14,,,1.00\n";
        out << "layer,tip_refine,1,0.85,ellipse,0.50,0.50,0.16,0.16,0.00,0.00,3.1415926,0.35,,1,0.97,1.00,0.00,0.00,0.00,0.00,0.02,0.00,0.01,-0.08,0.85,0.10,0.018,0.000,0.50,0.14,,,1.00\n";
        out << "layer,single_strand_fix,1,1.00,all,0.50,0.50,0.20,0.20,0.00,0.00,3.1415926,0.20,,0,1.00,1.00,0.00,0.00,0.00,0.00,0.01,0.00,0.00,0.10,0.00,0.00,0.00,0.000,0.50,0.14,42,replace,1.00\n";
        outError.clear();
        return true;
    }

    bool WriteEditRuntimeStateJson(const std::filesystem::path& path,
                                   const EditStack& stack,
                                   const RuntimeEditState& state,
                                   const HairAsset& asset,
                                   std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to write runtime edit state json: " + path.string();
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"layerCount\": " << stack.layers.size() << ",\n";
        out << "  \"boundarySmoothing\": " << stack.boundarySmoothing << ",\n";
        out << "  \"boundaryRadiusUv\": " << stack.boundaryRadiusUv << ",\n";
        out << "  \"activeGuideCount\": " << state.activeGuideCount << ",\n";
        out << "  \"guideCount\": " << asset.guideStrandCount << ",\n";
        out << "  \"strandCount\": " << asset.strands.size() << ",\n";
        out << "  \"maxGuideInfluence\": " << state.maxGuideInfluence << ",\n";
        out << "  \"activeGuideRatio\": " << (asset.guideStrandCount > 0u ? (static_cast<double>(state.activeGuideCount) / static_cast<double>(asset.guideStrandCount)) : 0.0) << ",\n";
        out << "  \"layers\": [\n";
        for (size_t i = 0; i < stack.layers.size(); ++i)
        {
            const EditLayer& layer = stack.layers[i];
            out << "    {\n";
            out << "      \"name\": \"" << JsonEscape(layer.name) << "\",\n";
            out << "      \"enabled\": " << (layer.enabled ? "true" : "false") << ",\n";
            out << "      \"layerWeight\": " << layer.layerWeight << ",\n";
            out << "      \"mode\": \"" << RegionModeToString(layer.region.mode) << "\",\n";
            out << "      \"guidesOnly\": " << (layer.region.guidesOnly ? "true" : "false") << ",\n";
            out << "      \"groupId\": ";
            if (layer.region.groupId == UINT32_MAX)
            {
                out << "null";
            }
            else
            {
                out << layer.region.groupId;
            }
            out << "\n    }";
            if (i + 1u < stack.layers.size())
            {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
        out << "  \"guideInfluence\": [";
        for (size_t i = 0; i < state.guideInfluence.size(); ++i)
        {
            if (i != 0u)
            {
                out << ", ";
            }
            out << state.guideInfluence[i];
        }
        out << "],\n";
        out << "  \"strandInfluence\": [";
        for (size_t i = 0; i < state.strandInfluence.size(); ++i)
        {
            if (i != 0u)
            {
                out << ", ";
            }
            out << state.strandInfluence[i];
        }
        out << "]\n}";
        out << "\n";
        outError.clear();
        return true;
    }

    bool WriteEditComparisonJson(const std::filesystem::path& path,
                                 const HairEditComparisonReport& report,
                                 std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to write edit comparison json: " + path.string();
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"rmsPositionError\": " << report.shape.rmsPositionError << ",\n";
        out << "  \"silhouetteError\": " << report.shape.silhouetteError << ",\n";
        out << "  \"clumpPreservationScore\": " << report.shape.clumpPreservationScore << ",\n";
        out << "  \"rootLocalityScore\": " << report.shape.rootLocalityScore << ",\n";
        out << "  \"editLocalityScore\": " << report.editLocalityScore << ",\n";
        out << "  \"boundaryNaturalnessScore\": " << report.boundaryNaturalnessScore << ",\n";
        out << "  \"editedStrandRatio\": " << report.editedStrandRatio << ",\n";
        out << "  \"editToRunLatencyMilliseconds\": " << report.editToRunLatencyMilliseconds << "\n";
        out << "}\n";
        outError.clear();
        return true;
    }


    struct ExperimentSuiteSettings
    {
        uint32_t samplesPerStrand = 16u;
        ActiveSetClusterSettings clusterSettings = {};
        ActiveSetBudget proposalBudget = {};
    };

    struct ExperimentVariantSummary
    {
        std::string name;
        bool usesEditScores = false;
        ActiveSetBudget budget = {};
        ActiveSetPlan plan = {};
    };

    bool WriteMetricDefinitionsJson(const std::filesystem::path& path, std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to write metric definitions json: " + path.string();
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"editMetrics\": [\n";
        out << "    {\n";
        out << "      \"name\": \"rmsPositionError\",\n";
        out << "      \"category\": \"shape\",\n";
        out << "      \"formula\": \"sqrt(mean(||p_ref(t)-p_edit(t)||^2)) over all sampled strand points\",\n";
        out << "      \"interpretation\": \"Lower is better; measures average spatial deviation after editing.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"silhouetteError\",\n";
        out << "      \"category\": \"shape\",\n";
        out << "      \"formula\": \"sqrt(mean((x_ref-x_edit)^2 + (z_ref-z_edit)^2)) over sampled points\",\n";
        out << "      \"interpretation\": \"Lower is better; approximates screen-space outer contour change using the XZ projection.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"clumpPreservationScore\",\n";
        out << "      \"category\": \"shape\",\n";
        out << "      \"formula\": \"clamp(1 - averageLengthDelta / (0.35 * averageReferenceLength + eps), 0, 1)\",\n";
        out << "      \"interpretation\": \"Higher is better; rewards edits that preserve strand-group length structure.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"rootLocalityScore\",\n";
        out << "      \"category\": \"shape\",\n";
        out << "      \"formula\": \"clamp(1 - averageRootDisplacement / (0.10 * assetBoundsDiagonal + eps), 0, 1)\",\n";
        out << "      \"interpretation\": \"Higher is better; indicates how stable root positions stay after editing.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"editLocalityScore\",\n";
        out << "      \"category\": \"editing\",\n";
        out << "      \"formula\": \"1 - editedStrandRatio, where a strand is edited when RMS strand delta > 0.01 * averageReferenceLength\",\n";
        out << "      \"interpretation\": \"Higher is better; estimates how much of the hairstyle remained untouched outside the intended region.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"boundaryNaturalnessScore\",\n";
        out << "      \"category\": \"editing\",\n";
        out << "      \"formula\": \"clamp(1 - averageNeighborDisplacementJump / (0.08 * averageReferenceLength + eps), 0, 1)\",\n";
        out << "      \"interpretation\": \"Higher is better; compares nearby guide strands across UV-space boundaries to penalize visible discontinuities.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"editedStrandRatio\",\n";
        out << "      \"category\": \"editing\",\n";
        out << "      \"formula\": \"changedStrandCount / min(referenceStrands, editedStrands)\",\n";
        out << "      \"interpretation\": \"Lower is better when locality is important; reports the fraction of strands whose sampled deformation exceeds the edit threshold.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"editToRunLatencyMilliseconds\",\n";
        out << "      \"category\": \"timing\",\n";
        out << "      \"formula\": \"wall-clock milliseconds spent applying the edit stack and preparing runtime state\",\n";
        out << "      \"interpretation\": \"Lower is better; approximates interactive response time from edit commit to runtime-ready output.\"\n";
        out << "    }\n";
        out << "  ],\n";
        out << "  \"runtimeMetrics\": [\n";
        out << "    {\n";
        out << "      \"name\": \"selectedClusterCount\",\n";
        out << "      \"formula\": \"number of active-set clusters selected under the current budget\",\n";
        out << "      \"interpretation\": \"Higher generally means more corrective work scheduled this frame.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"selectedGuideCount\",\n";
        out << "      \"formula\": \"total guide strands promoted to full corrective simulation\",\n";
        out << "      \"interpretation\": \"Higher means more of the hairstyle receives high-cost correction.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"estimatedFrameMilliseconds\",\n";
        out << "      \"formula\": \"estimatedFixedCostMilliseconds + selectedClusterCount * estimatedClusterCostMilliseconds\",\n";
        out << "      \"interpretation\": \"Predicted frame cost used for bounded-latency reasoning; lower is safer for the 16.67 ms budget.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"recommendedSolverIterations\",\n";
        out << "      \"formula\": \"budget-adapted iteration recommendation derived from the current budget scale\",\n";
        out << "      \"interpretation\": \"Higher means the runtime can afford more local corrective iterations this frame.\"\n";
        out << "    }\n";
        out << "  ]\n";
        out << "}\n";
        outError.clear();
        return true;
    }

    bool BuildEmptyVariantPlan(const HairAsset& asset,
                              const std::vector<ActiveSetCluster>& clusters,
                              const ActiveSetBudget& budget,
                              ExperimentVariantSummary& outVariant,
                              std::string& outError);

