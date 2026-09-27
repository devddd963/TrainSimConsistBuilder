#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace StockSpecReader
{
    struct StockDimensions
    {
        float widthM = 0.0f;
        float heightM = 0.0f;
        float lengthM = 0.0f;
    };

    struct StockCoupler
    {
        std::wstring type = L"Automatic";
        float r0_min = 0.15f;    // Coupler.R0X (meters)
        float r0_max = 0.15f;    // Coupler.R0Y (meters)
        float stiffness1 = 5e6f; // N/m
        float stiffness2 = 0.0f;
        float damping1 = 1e6f;   // N/m/s
        float damping2 = 0.0f;
        float break1 = 5.2e6f;   // N
        float break2 = 5.2e6f;
        bool hasRigidConnection = false;
        float velocity = 0.15f;  // m/s

        std::wstring rawR0;
        std::wstring rawStiffness;
        std::wstring rawDamping;
        std::wstring rawBreak;
        std::wstring rawVelocity;
    };

    struct StockBuffer
    {
        float r0_min = 0.0f;
        float r0_max = 1e9f;
        float stiffness1 = 5e6f;
        float stiffness2 = 5e6f;
        float damping1 = 1e6f;
        float damping2 = 1e6f;
        float centre = 0.5f;
        float radius = 1.0f;
        float angleDeg = 0.5f;
        bool exists = false;

        std::wstring rawR0;
        std::wstring rawStiffness;
        std::wstring rawDamping;
    };

    struct StockFreightAnim
    {
        std::wstring shapePath;
        std::wstring fullPath;
        bool existsOnDisk = false;
        float width = 0.0f;
        float height = 0.0f;
        float length = 0.0f;
        bool isContinuous = false;
    };

    struct StockSpec
    {
        std::wstring filePath;
        std::wstring fileName;
        std::wstring folderName;
        std::wstring extension;     // .eng or .wag

        // Identifiers
        std::wstring displayName;   // Name ( "..." )
        std::wstring wagonName;     // Wagon ( "..." )
        std::wstring engineName;    // Engine ( "..." )
        std::wstring category;      // Diesel, Electric, Steam, Control, Passenger, Freight, Tender
        std::wstring rawType;       // Engine/Wagon Type keyword
        
        // Physical Geometry
        StockDimensions size;       // Size ( width, height, length )
        std::wstring mainShapeFile; // WagonShape ( "..." )
        bool shapeExistsOnDisk = false;
        std::wstring fullShapePath;
        std::vector<StockFreightAnim> freightAnims;

        // Couplers & Buffers
        std::vector<StockCoupler> couplers;
        StockBuffer buffers;

        // Technical Parameters
        float massKg = 0.0f;
        float maxPowerKw = 0.0f;
        float maxForceKn = 0.0f;
        float maxVelocityKmh = 0.0f;
        float maxContinuousForceKn = 0.0f;
        float maxBrakeForceKn = 0.0f;
        std::wstring brakeSystemType;
        std::wstring cabViewFile;
        std::wstring insideSoundFile;
        std::wstring outsideSoundFile;

        // ORTS Extended Physics
        float ortsLengthBogieCentreM = 0.0f;
        float ortsLengthCarBodyM = 0.0f;
        float ortsLengthCouplerFaceM = 0.0f;
        float ortsLengthAirHoseM = 0.0f;
        float ortsWheelFlangeLengthM = 0.0f;

        // Diagnostic / Trace Info
        std::vector<std::wstring> resolvedIncludes;
        std::vector<std::wstring> missingIncludes;
        bool isValid = false;
        bool fileExistsOnDisk = true;

        // Helper calculations
        float GetCouplerZeroLengthM() const
        {
            if (couplers.empty()) return 0.0f;
            return couplers[0].r0_min;
        }

        float GetEffectivePlacementStepM() const
        {
            float len = (size.lengthM > 0.0f) ? size.lengthM : 10.0f;
            float couplerR0 = GetCouplerZeroLengthM();
            return len + couplerR0;
        }
    };

    // Fast Summary Mode for Stock Library Directory Scanning
    bool ReadSummary(
        const std::wstring& filePath,
        std::wstring& outName,
        std::wstring& outType,
        std::wstring& outPower,
        std::wstring& outMass,
        std::wstring& outCabView,
        const std::wstring& trainsetBasePath = L"");

    // Deep Spec Mode for Full Physics, Couplers, Sizes, and Shapes
    StockSpec ReadFullSpec(
        const std::wstring& filePath,
        const std::wstring& trainsetBasePath = L"");

    // Unit parsing helpers
    float ParseDistanceUnit(const std::string& valStr, float defaultM = 0.0f);
    float ParseForceUnit(const std::string& valStr, float defaultN = 0.0f);
    float ParseMassUnit(const std::string& valStr, float defaultKg = 0.0f);
    float ParsePowerUnit(const std::string& valStr, float defaultKw = 0.0f);
    float ParseSpeedUnit(const std::string& valStr, float defaultKmh = 0.0f);
}
