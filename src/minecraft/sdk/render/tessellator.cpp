#include "minecraft/sdk/render/tessellator.h"
#include "minecraft/hooks/hook_memory.h"
#include "minecraft/bedrock_memory.h"

using namespace minecraft;

void Tessellator::vertex(float x, float y, float z) {
    static std::uintptr_t func = hook_memory::resolve_signature(BedrockSignatures::render_tessVertex);
    if (!func) return;
    reinterpret_cast<void(*)(Tessellator*, float, float, float)>(func)(this, x, y, z);
}

void Tessellator::begin(Primitive primitive, int numVertices) {
    static std::uintptr_t func = hook_memory::resolve_signature(BedrockSignatures::render_tessBegin);
    if (!func) return;
    reinterpret_cast<void(*)(Tessellator*, int, Primitive, int, bool)>(func)(
        this, 0, primitive, numVertices, false);
}

void Tessellator::color(float r, float g, float b, float a) {
    static std::uintptr_t func = hook_memory::resolve_signature(BedrockSignatures::render_tessColor);
    if (!func) return;
    reinterpret_cast<void(*)(Tessellator*, float, float, float, float)>(func)(this, r, g, b, a);
}

void Tessellator::color(const mce::Color& col) {
    color(col.r, col.g, col.b, col.a);
}
