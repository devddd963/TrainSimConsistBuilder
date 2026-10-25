#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>
#include <cwchar>

enum class SmsFileEncoding {
    Utf16LeBom,
    Utf8Bom,
    Utf8NoBom,
    Ansi
};

struct SmsCurvePoint {
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const SmsCurvePoint& o) const {
        return std::fabs(x - o.x) < 1e-4f && std::fabs(y - o.y) < 1e-4f;
    }
    bool operator!=(const SmsCurvePoint& o) const { return !(*this == o); }
};

struct SmsCurve {
    std::wstring curveType;     // "FrequencyCurve" or "VolumeCurve"
    std::wstring controlType;   // "SpeedControlled", "Variable1Controlled", "Variable2Controlled", "Variable3Controlled", "BrakeCylControlled", "DistanceControlled"
    std::vector<SmsCurvePoint> points;
    int declaredPoints = 0;
    float granularity = 0.0f;

    // UI state
    bool isCollapsed = true;

    bool operator==(const SmsCurve& o) const {
        return _wcsicmp(curveType.c_str(), o.curveType.c_str()) == 0 &&
               _wcsicmp(controlType.c_str(), o.controlType.c_str()) == 0 &&
               std::fabs(granularity - o.granularity) < 1e-4f &&
               points == o.points;
    }
    bool operator!=(const SmsCurve& o) const { return !(*this == o); }
};

struct SmsFileRef {
    std::wstring fileName;         // Raw token, e.g. "WA5_1.wav" or "./HORNS/GZB_WAP5.wav"
    int flag = -1;                 // Usually -1
    std::wstring resolvedDiskPath; // Absolute path if resolved
    bool existsOnDisk = false;

    bool operator==(const SmsFileRef& o) const {
        return flag == o.flag && _wcsicmp(fileName.c_str(), o.fileName.c_str()) == 0;
    }
    bool operator!=(const SmsFileRef& o) const { return !(*this == o); }
};

struct SmsTrigger {
    std::wstring triggerType;      // "Initial_Trigger", "Discrete_Trigger", "Variable_Trigger", "Random_Trigger", "Dist_Travelled_Trigger", "Joint_*Axle_Trigger", "Switch_*Axle_Trigger", "Xover_*Axle_Trigger"
    int triggerId = 0;             // For Discrete_Trigger (e.g. 8 for Horn, 4 for Sander, 10 for Bell)
    std::wstring variableCondition;// For Variable_Trigger (e.g. "Speed_Inc_Past 15.0")
    float delayMin = 0.0f;         // For Random_Trigger Delay_Min_Max
    float delayMax = 0.0f;
    float distMin = 0.0f;          // For Dist_Travelled_Trigger Dist_Min_Max
    float distMax = 0.0f;
    float carCameraDist = 80.0f;   // For Joint/Switch/Xover triggers Car_Camera_Dist (default in OR is 80m)
    bool hasCarCameraDist = false; // Whether Car_Camera_Dist was explicitly specified
    float volumeMin = 1.0f;        // For Random_Trigger / Dist_Travelled_Trigger Volume_Min_Max (optional)
    float volumeMax = 1.0f;
    bool hasVolumeMinMax = false;
    std::wstring actionType;       // "PlayOneShot", "StartLoop", "StartLoopRelease", "ReleaseLoopRelease", "ReleaseLoopReleaseWithJump", "SetStreamVolume", etc.
    int declaredFiles = 0;
    std::vector<SmsFileRef> files;
    std::wstring selectionMethod;  // "SequentialSelection", "RandomSelection"
    float actionParam = 0.0f;      // For SetStreamVolume etc.

    // UI state
    bool isCollapsed = true;

    bool operator==(const SmsTrigger& o) const {
        return triggerId == o.triggerId &&
               hasCarCameraDist == o.hasCarCameraDist &&
               hasVolumeMinMax == o.hasVolumeMinMax &&
               std::fabs(delayMin - o.delayMin) < 1e-4f &&
               std::fabs(delayMax - o.delayMax) < 1e-4f &&
               std::fabs(distMin - o.distMin) < 1e-4f &&
               std::fabs(distMax - o.distMax) < 1e-4f &&
               std::fabs(carCameraDist - o.carCameraDist) < 1e-4f &&
               std::fabs(volumeMin - o.volumeMin) < 1e-4f &&
               std::fabs(volumeMax - o.volumeMax) < 1e-4f &&
               std::fabs(actionParam - o.actionParam) < 1e-4f &&
               _wcsicmp(triggerType.c_str(), o.triggerType.c_str()) == 0 &&
               _wcsicmp(variableCondition.c_str(), o.variableCondition.c_str()) == 0 &&
               _wcsicmp(actionType.c_str(), o.actionType.c_str()) == 0 &&
               _wcsicmp(selectionMethod.c_str(), o.selectionMethod.c_str()) == 0 &&
               files == o.files;
    }
    bool operator!=(const SmsTrigger& o) const { return !(*this == o); }
};

struct SmsStream {
    int streamIndex = 0;
    std::wstring friendlyTitle;    // Extracted from Skip( **** Title **** ) or "Stream N"
    int priority = 6;
    float volume = 1.0f;
    bool hasVolume = false;
    bool isStereo = false;
    int declaredTriggers = 0;
    std::vector<SmsTrigger> triggers;
    std::vector<SmsCurve> curves;

    // UI state
    bool isCollapsed = true;
    bool isPlayingPreview = false;
    bool isSelected = false;

    bool operator==(const SmsStream& o) const {
        return priority == o.priority &&
               hasVolume == o.hasVolume &&
               isStereo == o.isStereo &&
               std::fabs(volume - o.volume) < 1e-4f &&
               triggers == o.triggers &&
               curves == o.curves;
    }
    bool operator!=(const SmsStream& o) const { return !(*this == o); }
};

struct StreamPresetItem {
    int id = 0;
    std::wstring name;
    std::wstring category = L"Custom";
    int priority = 6;
    float volume = 1.0f;
    bool isStereo = false;
    std::wstring primaryWav;
    std::vector<SmsTrigger> triggers;
    std::vector<SmsCurve> curves;
};

struct StreamPresetCard {
    int id = 0;
    std::wstring name;
    std::wstring category = L"Custom";
    bool isCollapsed = false;
    std::vector<StreamPresetItem> streams;
};

struct SmsScalabilityGroup {
    int groupLevel = 5;            // 5, 3, 1, 0
    bool hasCabCam = false;
    bool hasPassengerCam = false;
    bool hasExternalCam = false;
    float activationDistance = 900.0f;
    float deactivationDistance = 630.0f;
    bool deactCabCam = false;
    bool deactPassengerCam = false;
    bool deactExternalCam = false;
    float groupVolume = 1.0f;
    bool hasGroupVolume = false;
    bool isStereo = false;
    int trackType = -1;            // -1 if not specified, otherwise 0, 1, 2...
    int deactTrackType = -1;       // -1 if not specified, otherwise 0, 1, 2...
    bool isSgPropsCollapsed = false;

    int declaredStreams = 0;
    std::vector<SmsStream> streams;

    bool operator==(const SmsScalabilityGroup& o) const {
        return groupLevel == o.groupLevel &&
               hasCabCam == o.hasCabCam &&
               hasPassengerCam == o.hasPassengerCam &&
               hasExternalCam == o.hasExternalCam &&
               deactCabCam == o.deactCabCam &&
               deactPassengerCam == o.deactPassengerCam &&
               deactExternalCam == o.deactExternalCam &&
               hasGroupVolume == o.hasGroupVolume &&
               isStereo == o.isStereo &&
               trackType == o.trackType &&
               deactTrackType == o.deactTrackType &&
               std::fabs(activationDistance - o.activationDistance) < 1e-4f &&
               std::fabs(deactivationDistance - o.deactivationDistance) < 1e-4f &&
               std::fabs(groupVolume - o.groupVolume) < 1e-4f &&
               streams == o.streams;
    }
    bool operator!=(const SmsScalabilityGroup& o) const { return !(*this == o); }
};

struct SmsDocument {
    std::wstring filePath;
    SmsFileEncoding encoding = SmsFileEncoding::Utf16LeBom;
    std::vector<std::wstring> headerComments;
    std::vector<SmsScalabilityGroup> groups;
    int activeGroupIndex = 0;

    bool isModified = false;
    bool isValid = true;
    std::vector<std::wstring> syntaxWarnings;
    std::vector<std::wstring> autoRepairLog;
};

struct SmsClipboardStream {
    SmsStream stream;
    std::vector<std::vector<std::wstring>> resolvedWavPaths;
};

struct SmsClipboard {
    std::wstring sourceSmsPath;
    std::vector<SmsClipboardStream> streams;
};

SmsClipboard& SmsClipboard_Get();

// Parser & Serializer Functions
bool SmsParser_LoadFromFile(const std::wstring& filePath, const std::wstring& basePath, SmsDocument& outDoc);
bool SmsParser_SaveToFile(const SmsDocument& doc, const std::wstring& filePath);
void SmsParser_DetectDiscrepancies(SmsDocument& doc);
void SmsParser_FixAllCounts(SmsDocument& doc);
void SmsParser_AutoRepairAndNormalize(SmsDocument& doc);
std::wstring SmsParser_GetDiscreteTriggerName(int triggerId);
std::wstring SmsParser_NormalizeRelativePath(const std::wstring& path);
std::wstring SmsParser_ResolveWavPath(const std::wstring& smsFilePath, const std::wstring& wavRef, const std::wstring& basePath);
std::wstring SmsParser_RetargetWavRelativePath(const std::wstring& targetSmsPath, const std::wstring& resolvedWavDiskPath, const std::wstring& basePath);
bool SmsParser_IsRecognizedTriggerType(const std::wstring& ttype);
bool SmsParser_IsTrackAxleTrigger(const std::wstring& ttype);
std::wstring SmsParser_FormatTriggerDisplayName(const SmsTrigger& trig);
