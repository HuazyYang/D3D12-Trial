#ifndef LIGHT_UTILS_H
#define LIGHT_UTILS_H
#include "d3dUtils.h"
#include <DirectXMath.h>

struct Light {
    DirectX::XMFLOAT3 Strength;   /// Light color
    FLOAT FalloutStart;          /// Point/Spot light only
    DirectX::XMFLOAT3 Direction; /// Directinal/Spot light only.
    FLOAT FalloutEnd;            /// Point/Spot light only
    DirectX::XMFLOAT3 Position;  /// Point/Spot light only
    FLOAT SpotPower;             /// Spot light only.
};

struct Lights {
    DirectX::XMFLOAT4 AmbientStrength;
    Light Lights[16];
};

#endif