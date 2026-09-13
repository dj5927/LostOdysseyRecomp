#include "../../LostOdysseyRecomp/cpu/particle_material_compat.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

int main()
{
    using particle_material_compat::Decision;
    constexpr uint32_t material = 0x1000, resource = 0x2000;
    constexpr uint32_t shaderMap = 0x3000, maps = 0x4000, particleMap = 0x5000;
    std::unordered_map<uint32_t, uint32_t> words{
        {material, 0x8200FAB0}, {material + 176, resource},
        {resource, 0x82003928}, {resource + 48, shaderMap},
        {shaderMap + 28, maps}, {shaderMap + 32, 1}, {shaderMap + 36, 1},
        {maps, particleMap}, {particleMap + 20, 0x8336CA10}, {particleMap + 4, 0}
    };
    uint8_t blendMode = 2;
    const auto check = [&] {
        return particle_material_compat::CheckRawMaterial(material,
            [&](uint32_t address) { return words.at(address); },
            [&](uint32_t address) { assert(address == material + 80); return blendMode; });
    };
    // Cooked raw shader material with a present but empty particle map.
    assert(check() == Decision::MissingParticleShaders);
    words[particleMap + 4] = 2;
    assert(check() == Decision::PreserveGuestResult);
    // A different VF's shader entries cannot satisfy particle compatibility.
    words[particleMap + 20] = 0x83360000;
    assert(check() == Decision::MissingParticleShaders);
    words[particleMap + 20] = 0x8336CA10;
    words[particleMap + 4] = 0;
    // Ordinary materials and other blend modes keep the engine's own decision.
    words[material] = 0x82001000;
    assert(check() == Decision::PreserveGuestResult);
    words[material] = 0x8200FAB0;
    for (const uint8_t mode : {uint8_t(0), uint8_t(1), uint8_t(3)})
    {
        blendMode = mode;
        assert(check() == Decision::PreserveGuestResult);
    }
    blendMode = 2;
    // Not-yet-ready resources already have a fallback in GetMaterial.
    words[material + 176] = 0;
    assert(check() == Decision::PreserveGuestResult);
    words[material + 176] = resource;
    words[resource + 48] = 0;
    assert(check() == Decision::PreserveGuestResult);
    words[resource + 48] = shaderMap;
    words[shaderMap + 32] = 2;
    assert(check() == Decision::PreserveGuestResult); // malformed count/capacity
    words[shaderMap + 32] = 0;
    words[shaderMap + 28] = 0;
    assert(check() == Decision::MissingParticleShaders); // ready map, no VF entries
    puts("PASS: raw particle compatibility policy (not guest ABI or rendered output)");
}
