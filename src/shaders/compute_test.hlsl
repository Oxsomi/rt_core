#include "../../core3/src/graphics/shaders/resources.hlsl"

[numthreads(16, 16, 1)]
void main(I32x2 i : _globalId) {
    rwTexture2DfUniform(0)[i] = F32x4((i.x & 3) / 4.0, (i.y & 3) / 4.0, 0, 1);
}
