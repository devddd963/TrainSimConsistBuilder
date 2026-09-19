#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include "../src/PoolMutator.h"

// Displays the Consist Pool Mutator & Injector Dialog
void ShowPoolMutatorDialog(
    HWND hWndParent,
    PoolMutator::MutatorMode initialMode = PoolMutator::MutatorMode::MutateConsists,
    const std::vector<std::wstring>& targetConsistFilePaths = {},
    const std::vector<int>& targetSelectedUnitIndices = {}
);
