#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <DirectXMath.h>
#include <vector>
#include "ShapeReader.h"

// =========================================================================
// ShapeAnimator - High-Performance 3D Runtime Animation Engine
// Handles:
// - Hierarchical Keyframe Matrix Evaluation (Quaternion SLERP + Position LERP)
// - MSTS Orientation Conjugate Basis Transformation (Zero parser tampering)
// - Procedural Pitch Wheel / Axle Rolling Simulation
// - Category-based Track Filtering (Pantographs, Doors, Wipers, Fans, etc.)
// =========================================================================
class ShapeAnimator {
public:
    ShapeAnimator() = default;
    ~ShapeAnimator() = default;

    // Evaluates all hierarchical bone matrices for a given frame and animation filter
    static void ComputeAnimatedMatrices(
        const ParsedShape& shape,
        float currentFrame,
        int filterType, // 0 = All, 1 = Pantos, 2 = Doors, 3 = Wipers, 4 = Keyframed Wheels, 5 = Fans, 6 = Driver, 7 = Displays, 8 = Custom, >=100 specific keyframe node index, 20 = All Simulated Wheels, >=200 specific simulated wheel node index
        float wheelSpinAngle,
        bool enableWheelSpin,
        std::vector<DirectX::XMFLOAT4X4>& outWorldMatrices
    );
};
