#include "../../core3/src/shaders/global_registers.hlsl"

[numthreads(16, 16, 1)]
void main(I32x2 i : _globalId) {
    F32x4 v = F32x4((i.x & 3) / 4.0, (i.y & 3) / 4.0, 0, 1);
    write2Df((EResourceType_RWTexture2D << 18) | 0, i, v);
}
