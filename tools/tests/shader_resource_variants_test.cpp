#include "gpu/shader/resource_variants.h"
#include <chrono>
#include <iostream>
#include <map>

namespace variants = xenos::resources::variants;
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void Write(const fs::path& path, std::span<const uint8_t> data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    Require(bool(out), "fixture write failed");
}
static Bytes Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    Require(bool(in) && in.tellg() >= 0 && in.tellg() <= 262144, "invalid reference input");
    Bytes data(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    Require(bool(in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()))), "reference read failed");
    return data;
}

// Read only the original SDK container. The captured variant reference is not
// opened until generation from this independently hashed input is complete.
static Bytes OriginalContainer(const fs::path& path, uint64_t offset) {
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "original decoded SDK container unavailable");
    const auto word = [&](uint64_t at) {
        std::array<uint8_t,4> bytes{};
        input.seekg(std::streamoff(at));
        Require(bool(input.read(reinterpret_cast<char*>(bytes.data()),4)), "truncated SDK header");
        return uint32_t(bytes[0])<<24 | uint32_t(bytes[1])<<16 | uint32_t(bytes[2])<<8 | bytes[3];
    };
    Require(word(offset)==0x102a1101, "SDK container signature");
    const auto virtualSize=word(offset+4),physicalSize=word(offset+8),shader=word(offset+24);
    Require(virtualSize>=36 && virtualSize<=65536 && shader>=36 && uint64_t(shader)+36<=virtualSize,
            "SDK virtual/shader bounds");
    const auto start=word(offset+shader),size=word(offset+shader+4);
    Require(size>=12 && size<=262144 && size%12==0 && uint64_t(start)+size<=physicalSize,
            "SDK microcode bounds");
    Bytes bytes(size);
    input.seekg(std::streamoff(offset+virtualSize+start));
    Require(bool(input.read(reinterpret_cast<char*>(bytes.data()),size)), "truncated SDK microcode");
    Require(variants::detail::Hash(bytes)==0x11bc08f69da45bb3ull, "independent original SDK base identity");
    return bytes;
}
static void CpxOriginal(const fs::path& scratch,const fs::path& decoded,uint64_t offset,const fs::path& reference) {
    const auto original=OriginalContainer(decoded,offset);
    const auto directory=scratch/"original-cpx-only";
    fs::create_directories(directory);
    const auto source=directory/"vs_11bc08f69da45bb3.bin";
    Write(source,original);
    std::map<uint64_t,Bytes> outputs;
    const auto result=variants::GenerateFixedVariants(directory,[&](uint64_t hash,std::span<const uint8_t> bytes) {
        Require(variants::detail::Hash(bytes)==hash,"CPX generated output hash");
        Require(outputs.emplace(hash,Bytes(bytes.begin(),bytes.end())).second,"duplicate CPX output");
    });
    Require(result.verifiedBases==1 && !result.invalidBases &&
        result.missingBases+1==std::size(variants::detail::sources),"CPX test uses one original base only");
    Require(outputs.contains(0x1474db97dfc0afadull),"packed declaration must generate captured 1474 variant");
    Require(outputs.at(0x1474db97dfc0afadull)==Read(reference),"1474 bytes differ from captured reference");
    Require(Read(source)==original,"CPX generation preserves original SDK bytes");
    auto corrupt=original;corrupt[0]^=1;Write(source,corrupt);
    size_t calls=0;
    const auto rejected=variants::GenerateFixedVariants(directory,[&](auto,auto){++calls;});
    Require(rejected.invalidBases==1 && !rejected.generated && !calls,"corrupt CPX source cannot authorize variants");
    std::cout<<"PASS: isolated original SDK 11bc -> captured 1474 byte-identical; "
             <<result.generated<<" generated, source preserved, corrupt source rejected\n";
}
int main(int argc, char** argv) {
    try {
        Require(argc == 2 || argc == 4 || argc == 5 || argc == 6, "usage: shader_resource_variants_test scratch [original-source reference-354 [reference-linked]] OR scratch --cpx-original decoded-file offset reference-1474 OR scratch --linked-only original-source reference-fixed reference-linked");
        const auto scratch = fs::path(argv[1]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const bool linkedOnly=argc==6 && std::string(argv[2])=="--linked-only";
        if (linkedOnly) { --argc; ++argv; }
        if (argc==6) {
            Require(std::string(argv[2])=="--cpx-original","unknown focused fixture");
            CpxOriginal(scratch,argv[3],std::stoull(argv[4]),argv[5]); return 0;
        }
        const auto sourceCount=std::size(variants::detail::sources);
        const auto pixelCount=std::size(variants::detail::link::pixels);
        fs::create_directories(scratch / "empty");
        size_t calls = 0;
        const auto save = [&](auto, auto) { ++calls; };
        bool rejected=false;
        if (!linkedOnly) {
        const auto empty = variants::GenerateFixedVariants(scratch / "empty", save);
        Require(!calls && !empty.generated && empty.missingBases == sourceCount && !empty.invalidBases, "empty input must not synthesize shaders");
        const auto stopped = variants::GenerateFixedVariants(scratch / "empty", save, [] { return true; });
        Require(stopped.cancelled && !stopped.generated && !stopped.bytesRead && !calls, "early cancellation");
        rejected = false;
        try { variants::GenerateFixedVariants(scratch / "empty", {}); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "missing save callback must be rejected");
        fs::create_directories(scratch / "invalid");
        for (size_t i = 0; i < 3; ++i) {
            const auto& source = variants::detail::sources[i];
            // Correct length / too short / too long, all with wrong bytes.
            const size_t length = i == 0 ? source.size : i == 1 ? source.size - 1 : source.size + 1;
            Write(scratch / "invalid" / variants::detail::Name(source.hash), Bytes(length));
        }
        Write(scratch / "invalid" / "vs_0000000000000000.bin", Bytes(12));
        const auto invalid = variants::GenerateFixedVariants(scratch / "invalid", save);
        Require(!calls && !invalid.generated && invalid.invalidBases == 3 && invalid.missingBases + 3 == sourceCount,
                "wrong hash and malformed lengths must not generate; unrelated files ignored");
        Require(invalid.bytesRead == variants::detail::sources[0].size, "malformed lengths rejected before allocation/read");
        const auto linkedEmpty = variants::GenerateLinkedVariants(scratch / "empty", save);
        Require(!linkedEmpty.generated && linkedEmpty.missingBases == sourceCount && linkedEmpty.missingPixelSources == pixelCount,
                "empty input cannot generate linked shaders");
        const auto linkedStopped = variants::GenerateLinkedVariants(scratch / "empty", save, [] { return true; });
        Require(linkedStopped.cancelled && !linkedStopped.bytesRead && !calls, "linked early cancellation");
        std::cout << "PASS: empty input, cancellation, callback validation, wrong hash, short/long input, unknown names\n";
        if (argc == 2) return 0;
        }

        const auto sourceDirectory = fs::path(argv[2]), references = fs::path(argv[3]);
        std::map<uint64_t, Bytes> generated;
        const auto full = variants::GenerateFixedVariants(sourceDirectory, [&](uint64_t hash, std::span<const uint8_t> data) {
            Require(variants::detail::Hash(data) == hash, "callback hash must describe exact output bytes");
            Require(generated.emplace(hash, Bytes(data.begin(), data.end())).second, "duplicate generated output");
        });
        if (!linkedOnly) {
        Require(full.verifiedBases > 0 && full.verifiedBases + full.missingBases == sourceCount && !full.invalidBases && full.generated >= 354,
                "original sources must preserve at least the historical 354 candidates");
        // Generation is complete before any reference bytes are read.
        size_t referenceCount = 0;
        for (const auto& entry : fs::directory_iterator(references)) {
            if (entry.is_regular_file() && entry.path().extension() == ".bin") ++referenceCount;
        }
        Require(referenceCount == 354, "reference set must contain exactly 354 candidates");
        fs::create_directories(scratch / "generated");
        for (const auto& entry : fs::directory_iterator(references)) {
            if (!entry.is_regular_file() || entry.path().extension()!=".bin") continue;
            const auto data=Read(entry.path());
            const auto hash=variants::detail::Hash(data);
            Require(generated.contains(hash) && generated.at(hash)==data,
                    "historical candidate missing or differs from independent reference");
        }
        for (const auto& [hash,data]:generated)
            Write(scratch / "generated" / variants::detail::Name(hash),data);
        fs::create_directories(scratch / "one-source");
        const auto first=std::find_if(std::begin(variants::detail::sources),std::end(variants::detail::sources),
            [&](const auto& item){return fs::exists(sourceDirectory/variants::detail::Name(item.hash));});
        Require(first!=std::end(variants::detail::sources),"partial fixture needs one available original source");
        const auto name = variants::detail::Name(first->hash);
        auto code = Read(sourceDirectory / name);
        Write(scratch / "one-source" / name, code);
        calls = 0;
        const auto one = variants::GenerateFixedVariants(scratch / "one-source", save);
        Require(one.verifiedBases == 1 && one.missingBases + 1 == sourceCount && calls > 0 && calls < full.generated, "partial original source coverage");
        Require(Read(scratch / "one-source" / name) == code, "generation must leave input unchanged");
        calls = 0;
        const auto partial = variants::GenerateFixedVariants(scratch / "one-source", save, [&] { return calls != 0; });
        Require(partial.cancelled && partial.generated == 1 && calls == 1, "cancellation between outputs");
        rejected = false;
        try { variants::GenerateFixedVariants(scratch / "one-source", [](auto, auto) { throw std::runtime_error("save failed"); }); }
        catch (const std::runtime_error& error) { rejected = std::string(error.what()) == "save failed"; }
        Require(rejected, "save failures must propagate to the caller");
        code[0] ^= 1;
        Write(scratch / "one-source" / name, code);
        calls = 0;
        const auto corrupt = variants::GenerateFixedVariants(scratch / "one-source", save);
        Require(corrupt.invalidBases == 1 && !corrupt.generated && !calls, "one-byte corruption cannot use source metadata");
        std::cout << "PASS: historical 354 outputs preserved byte-identically; partial coverage, input preservation, mid-run cancellation, save failure, corruption\n"
                  << "bytesRead=" << full.bytesRead << " generated=" << (scratch / "generated").string() << '\n';
        }
        if (argc == 5) {
            const auto started = std::chrono::steady_clock::now();
            std::map<uint64_t, Bytes> linked;
            const auto expanded = variants::GenerateLinkedVariants(sourceDirectory, [&](uint64_t hash, std::span<const uint8_t> data) {
                Require(!generated.contains(hash), "linked API must exclude fixed candidates");
                Require(linked.emplace(hash, Bytes(data.begin(), data.end())).second, "duplicate linked output");
            });
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            Require(expanded.verifiedBases == full.verifiedBases && expanded.verifiedPixelSources == pixelCount &&
                    !expanded.invalidBases && !expanded.invalidPixelSources,
                    "original source signatures must remain valid for linked generation");
            // Compare only after generation is finished.
            fs::create_directories(scratch / "linked-generated");
            size_t linkedReferences=0;
            for (const auto& entry:fs::directory_iterator(argv[4])) {
                if (!entry.is_regular_file() || entry.path().extension()!=".bin") continue;
                ++linkedReferences;
                const auto data=Read(entry.path()); const auto hash=variants::detail::Hash(data);
                Require((linked.contains(hash) && linked.at(hash)==data) ||
                        (generated.contains(hash) && generated.at(hash)==data),
                        "historical linked reference missing or changed");
            }
            Require(linkedReferences==2245,"historical combined fixed/linked reference set incomplete");
            for (const auto& [hash,data]:linked)
                Write(scratch / "linked-generated" / variants::detail::Name(hash),data);
            calls = 0;
            const auto linkedPartial = variants::GenerateLinkedVariants(sourceDirectory, save, [&] { return calls != 0; });
            Require(linkedPartial.cancelled && calls == 1 && linkedPartial.generated == 1, "linked cancellation between outputs");
            rejected = false;
            try { variants::GenerateLinkedVariants(sourceDirectory, [](auto, auto) { throw std::runtime_error("linked save failed"); }); }
            catch (const std::runtime_error& error) { rejected = std::string(error.what()) == "linked save failed"; }
            Require(rejected, "linked save failure must propagate");
            fs::create_directories(scratch / "no-pixels");
            for (const auto& source : variants::detail::sources) {
                const auto inputName = variants::detail::Name(source.hash);
                if (fs::exists(sourceDirectory / inputName))
                    Write(scratch / "no-pixels" / inputName, Read(sourceDirectory / inputName));
            }
            const auto noPixels = variants::GenerateLinkedVariants(scratch / "no-pixels", [](auto, auto) {});
            Require(noPixels.missingPixelSources == pixelCount && noPixels.generated < expanded.generated,
                    "missing PS source must not authorize representative PS link plans");
            const auto& pixel = variants::detail::link::pixels[0];
            auto pixelName = variants::detail::Name(pixel.hash); pixelName[0] = 'p';
            Write(scratch / "no-pixels" / pixelName, Bytes(pixel.size));
            const auto badPixel = variants::GenerateLinkedVariants(scratch / "no-pixels", [](auto, auto) {});
            Require(badPixel.invalidPixelSources == 1 && badPixel.generated == noPixels.generated,
                    "corrupt PS source cannot authorize link metadata");
            std::cout << "PASS: historical 1891 SDK linked outputs preserved, PS hash gating, linked cancellation/save failure; ms="
                      << elapsed << " bytesRead=" << expanded.bytesRead << '\n';
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
