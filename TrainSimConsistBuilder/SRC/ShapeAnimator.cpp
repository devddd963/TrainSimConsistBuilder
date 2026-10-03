#include "ShapeAnimator.h"
#include <cmath>
#include <algorithm>

using namespace DirectX;

void ShapeAnimator::ComputeAnimatedMatrices(
    const ParsedShape& shape,
    float currentFrame,
    int filterType,
    float wheelSpinAngle,
    bool enableWheelSpin,
    std::vector<DirectX::XMFLOAT4X4>& outWorldMatrices
) {
    size_t numBones = shape.boneMatrices.size();
    if (numBones == 0) {
        outWorldMatrices.clear();
        return;
    }

    outWorldMatrices.resize(numBones);
    std::vector<XMMATRIX> localAnimated(numBones);

    for (size_t i = 0; i < numBones; ++i) {
        bool shouldAnimateKeyframes = false;
        bool shouldSimulateWheelRoll = false;

        const AnimNodeTrack* pTrack = nullptr;
        if (i < shape.animation.animNodes.size()) {
            pTrack = &shape.animation.animNodes[i];
            if (pTrack->hasAnim) {
                if (filterType == 0) {
                    shouldAnimateKeyframes = true;
                } else if (filterType == 1 && pTrack->type == AnimNodeType::Pantograph) {
                    shouldAnimateKeyframes = true;
                } else if (filterType == 2 && pTrack->type == AnimNodeType::DoorOrMirror) {
                    shouldAnimateKeyframes = true;
                } else if (filterType == 3 && pTrack->type == AnimNodeType::Wiper) {
                    shouldAnimateKeyframes = true;
                } else if (filterType == 4 && pTrack->type == AnimNodeType::WheelOrBogie) {
                    shouldAnimateKeyframes = true;
                } else if (filterType == 5 && pTrack->type == AnimNodeType::FanOrBlower) {
                    shouldAnimateKeyframes = true;
                } else if (filterType == 6 && pTrack->type == AnimNodeType::DriverOrCrew) {
                    shouldAnimateKeyframes = true;
                } else if (filterType == 7 && pTrack->type == AnimNodeType::DisplayOrBoard) {
                    shouldAnimateKeyframes = true;
                } else if (filterType == 8 && pTrack->type == AnimNodeType::Custom) {
                    shouldAnimateKeyframes = true;
                } else if (filterType >= 100 && filterType < 200 && (size_t)(filterType - 100) == i) {
                    shouldAnimateKeyframes = true;
                }
            }
        }

        // Check for simulated procedural wheel roll
        if (enableWheelSpin) {
            for (size_t w = 0; w < shape.animation.simulatedWheels.size(); ++w) {
                if (shape.animation.simulatedWheels[w].nodeIndex == (int32_t)i) {
                    if (filterType == 0 || filterType == 4 || filterType == 20) {
                        shouldSimulateWheelRoll = true;
                    } else if (filterType >= 200 && (size_t)(filterType - 200) == w) {
                        shouldSimulateWheelRoll = true;
                    }
                    break;
                }
            }
        }

        if (shouldAnimateKeyframes && pTrack) {
            // 1. Position Interpolation (LERP)
            XMVECTOR vPos = XMLoadFloat3(&pTrack->bindPos);
            if (!pTrack->posKeys.empty()) {
                if (pTrack->posKeys.size() == 1 || currentFrame <= pTrack->posKeys.front().frame) {
                    vPos = XMLoadFloat3(&pTrack->posKeys.front().pos);
                } else if (currentFrame >= pTrack->posKeys.back().frame) {
                    vPos = XMLoadFloat3(&pTrack->posKeys.back().pos);
                } else {
                    for (size_t k = 0; k + 1 < pTrack->posKeys.size(); ++k) {
                        if (currentFrame >= pTrack->posKeys[k].frame && currentFrame <= pTrack->posKeys[k + 1].frame) {
                            float f0 = pTrack->posKeys[k].frame;
                            float f1 = pTrack->posKeys[k + 1].frame;
                            float t = (f1 > f0) ? (currentFrame - f0) / (f1 - f0) : 0.0f;
                            XMVECTOR p0 = XMLoadFloat3(&pTrack->posKeys[k].pos);
                            XMVECTOR p1 = XMLoadFloat3(&pTrack->posKeys[k + 1].pos);
                            vPos = XMVectorLerp(p0, p1, t);
                            break;
                        }
                    }
                }
            }

            // 2. Rotation Quaternion Interpolation (SLERP with Conjugate Basis)
            // MSTS / GMax exporter stores quaternions as conjugate orientations q* = (-x, -y, -z, w).
            // Applying quaternion conjugation q = (-x, -y, -z, w) reconstructs the exact local orientation matrix.
            XMVECTOR vRot;
            if (!pTrack->rotKeys.empty()) {
                XMFLOAT4 qRaw;
                if (pTrack->rotKeys.size() == 1 || currentFrame <= pTrack->rotKeys.front().frame) {
                    qRaw = pTrack->rotKeys.front().quat;
                } else if (currentFrame >= pTrack->rotKeys.back().frame) {
                    qRaw = pTrack->rotKeys.back().quat;
                } else {
                    for (size_t k = 0; k + 1 < pTrack->rotKeys.size(); ++k) {
                        if (currentFrame >= pTrack->rotKeys[k].frame && currentFrame <= pTrack->rotKeys[k + 1].frame) {
                            float f0 = pTrack->rotKeys[k].frame;
                            float f1 = pTrack->rotKeys[k + 1].frame;
                            float t = (f1 > f0) ? (currentFrame - f0) / (f1 - f0) : 0.0f;
                            XMVECTOR q0 = XMLoadFloat4(&pTrack->rotKeys[k].quat);
                            XMVECTOR q1 = XMLoadFloat4(&pTrack->rotKeys[k + 1].quat);
                            XMVECTOR qs = XMQuaternionSlerp(q0, q1, t);
                            XMStoreFloat4(&qRaw, qs);
                            break;
                        }
                    }
                }
                vRot = XMVectorSet(-qRaw.x, -qRaw.y, -qRaw.z, qRaw.w);
            } else {
                vRot = XMLoadFloat4(&pTrack->bindRotQuat);
            }

            // Compose animated local transformation matrix
            XMMATRIX mRot = XMMatrixRotationQuaternion(vRot);
            XMMATRIX mTrans = XMMatrixTranslationFromVector(vPos);
            localAnimated[i] = mRot * mTrans;
        } else if (shouldSimulateWheelRoll && pTrack) {
            // Procedural Wheel Roll: Pitch rotation around local X-axis
            XMVECTOR vPos = XMLoadFloat3(&pTrack->bindPos);
            XMVECTOR vRot = XMLoadFloat4(&pTrack->bindRotQuat);
            XMMATRIX mSpin = XMMatrixRotationX(wheelSpinAngle);
            XMMATRIX mBaseRot = XMMatrixRotationQuaternion(vRot);
            XMMATRIX mCombinedRot = XMMatrixMultiply(mSpin, mBaseRot);
            XMMATRIX mTrans = XMMatrixTranslationFromVector(vPos);
            localAnimated[i] = mCombinedRot * mTrans;
        } else {
            localAnimated[i] = XMLoadFloat4x4(&shape.boneMatrices[i]);
        }
    }

    // Top-down hierarchy matrix accumulation: W[bone] = L[bone] * W[parent]
    std::vector<XMMATRIX> worldAnimated(numBones);
    for (size_t i = 0; i < numBones; ++i) {
        int parent = (i < shape.boneHierarchy.size()) ? shape.boneHierarchy[i] : -1;
        if (parent >= 0 && (size_t)parent < worldAnimated.size()) {
            worldAnimated[i] = XMMatrixMultiply(localAnimated[i], worldAnimated[parent]);
        } else {
            worldAnimated[i] = localAnimated[i];
        }
        XMStoreFloat4x4(&outWorldMatrices[i], worldAnimated[i]);
    }
}
