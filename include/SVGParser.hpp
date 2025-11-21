#pragma once
#include <vector>
#include <string>
#include <iostream>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

class SVGParser {
public:
    static std::vector<glm::vec2> LoadAndSample(const std::string& filepath, int numSamples = 3000) {
        std::cout << "Parsing SVG: " << filepath << std::endl;

        NSVGimage* image = nsvgParseFromFile(filepath.c_str(), "px", 96);
        if (!image) {
            std::cerr << "Failed to open SVG file." << std::endl;
            return {};
        }

        std::vector<glm::vec2> rawPoints;
        rawPoints.reserve(10000); 

        // 1. Extract Raw Bezier Samples
        for (NSVGshape* shape = image->shapes; shape != NULL; shape = shape->next) {
            for (NSVGpath* path = shape->paths; path != NULL; path = path->next) {
                for (int i = 0; i < path->npts - 1; i += 3) {
                    float* p = &path->pts[i * 2];
                    // Sample bezier curves heavily to capture detail
                    for (float t = 0; t < 1.0f; t += 0.05f) {
                        float it = 1.0f - t;
                        // Cubic Bezier Formula
                        float x = it*it*it*p[0] + 3*it*it*t*p[2] + 3*it*t*t*p[4] + t*t*t*p[6];
                        float y = it*it*it*p[1] + 3*it*it*t*p[3] + 3*it*t*t*p[5] + t*t*t*p[7];
                        rawPoints.push_back(glm::vec2(x, y));
                    }
                }
            }
        }
        
        nsvgDelete(image);

        if (rawPoints.empty()) {
             std::cerr << "No path data found in SVG." << std::endl;
             return {};
        }

        // 2. Normalize Raw Points BEFORE Resampling
        // This fixes issues with tiny or huge SVGs
        NormalizeInPlace(rawPoints, 1000.0f);

        // 3. Resample to fixed count (Uniform Arc Length)
        return ResampleByLength(rawPoints, numSamples);
    }

private:
    static void NormalizeInPlace(std::vector<glm::vec2>& points, float targetSize) {
        if (points.empty()) return;
        
        glm::vec2 minB(1e9), maxB(-1e9);
        for(const auto& p : points) {
            minB = glm::min(minB, p);
            maxB = glm::max(maxB, p);
        }

        glm::vec2 center = (minB + maxB) * 0.5f;
        float w = maxB.x - minB.x;
        float h = maxB.y - minB.y;
        float scale = targetSize / std::max(w, h);
        if (std::isnan(scale) || std::isinf(scale)) scale = 1.0f;

        for(auto& p : points) {
            p = (p - center) * scale;
            p.y *= -1.0f; // Flip Y for OpenGL
        }
    }

    static std::vector<glm::vec2> ResampleByLength(const std::vector<glm::vec2>& raw, int count) {
        if (raw.size() < 2) return raw;

        float totalLength = 0.0f;
        std::vector<float> cumulativeLength;
        cumulativeLength.reserve(raw.size());
        cumulativeLength.push_back(0.0f);

        for (size_t i = 0; i < raw.size() - 1; ++i) {
            float dist = glm::distance(raw[i], raw[i+1]);
            totalLength += dist;
            cumulativeLength.push_back(totalLength);
        }

        std::vector<glm::vec2> resampled;
        resampled.reserve(count);

        float step = totalLength / count;
        float currentDist = 0.0f;
        
        size_t rawIndex = 0;
        for (int i = 0; i < count; ++i) {
            // Find segment
            while (rawIndex < cumulativeLength.size() - 1 && cumulativeLength[rawIndex+1] < currentDist) {
                rawIndex++;
            }

            if (rawIndex >= raw.size() - 1) {
                resampled.push_back(raw.back());
            } else {
                float segmentStart = cumulativeLength[rawIndex];
                float segmentEnd = cumulativeLength[rawIndex+1];
                float segmentLen = segmentEnd - segmentStart;
                
                float t = 0.0f;
                if (segmentLen > 0.00001f) {
                    t = (currentDist - segmentStart) / segmentLen;
                }
                resampled.push_back(glm::mix(raw[rawIndex], raw[rawIndex+1], t));
            }
            currentDist += step;
        }

        return resampled;
    }
};