#pragma once

#include <cstdint>

namespace particle_material_compat
{
    enum class Decision { PreserveGuestResult, MissingParticleShaders };

    // Issue #16: the raw-material class advertises particle support
    // unconditionally, although its cooked mesh shader map may be empty.
    // Ordinary materials and resources which are not ready retain guest policy.
    template<class Read32, class Read8>
    Decision CheckRawMaterial(uint32_t material, Read32 read32, Read8 read8)
    {
        constexpr uint32_t rawMaterialVtable = 0x8200FAB0;
        constexpr uint32_t rawResourceVtable = 0x82003928;
        constexpr uint32_t particleVertexFactory = 0x8336CA10;
        if (!material || read32(material) != rawMaterialVtable || read8(material + 80) != 2)
            return Decision::PreserveGuestResult;

        const uint32_t resource = read32(material + 176);
        if (!resource || read32(resource) != rawResourceVtable)
            return Decision::PreserveGuestResult;
        const uint32_t shaderMap = read32(resource + 48);
        if (!shaderMap)
            return Decision::PreserveGuestResult; // GetMaterial already falls back here.

        // Serialized mesh-map array, from which 824E5290 builds the VF lookup.
        const uint32_t maps = read32(shaderMap + 28);
        const uint32_t count = read32(shaderMap + 32);
        const uint32_t capacity = read32(shaderMap + 36);
        if (count > capacity || count > 1024 || (count && !maps))
            return Decision::PreserveGuestResult; // Not a malformed-memory recovery hook.
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t meshMap = read32(maps + i * 4);
            if (!meshMap)
                return Decision::PreserveGuestResult;
            if (read32(meshMap + 20) == particleVertexFactory)
                return read32(meshMap + 4) == 0
                    ? Decision::MissingParticleShaders : Decision::PreserveGuestResult;
        }
        return Decision::MissingParticleShaders;
    }
}
