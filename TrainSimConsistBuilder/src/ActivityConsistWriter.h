#pragma once

#include <string>
#include <vector>
#include "ActivityConsistReader.h"
#include "ConsistReader.h"

namespace ActivityConsistWriter {

    struct SaveResult {
        bool success = false;
        std::wstring errorMessage;
    };

    /**
     * Creates a safe .act.bak backup file before modifying.
     */
    bool CreateBackup(const std::wstring& actFilePath);

    /**
     * Surgically updates a loose consist inside an .act file's ActivityObjects block,
     * re-indexing all units with zero-indexed sequential UiDs (UiD ( 0 ), UiD ( 1 ), ...),
     * and preserving original encoding (UTF-16 LE with BOM or ASCII).
     */
    SaveResult SaveActivityConsist(
        const std::wstring& actFilePath,
        int targetObjectIndex,
        const std::wstring& targetObjectId,
        const std::vector<ConsistReader::UnitInfo>& updatedUnits,
        const std::wstring& consistName = L""
    );
}
