#include "gpu/shader/portable_shader_pack.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc,char** argv) try {
    if(argc!=3 || (std::string_view(argv[1])!="inspect" && std::string_view(argv[1])!="verify")) {
        std::cerr<<"Usage: LoShaderPackTool <inspect|verify> path/to/portable_vk.lospv\n";
        return 2;
    }
    const bool verified=std::string_view(argv[1])=="verify";
    const auto r=xenos::portable_pack::Reader::Inspect(std::filesystem::path(reinterpret_cast<const char8_t*>(argv[2])),verified);
    std::cout<<"{\n  \"schema\": "<<xenos::portable_pack::Schema
        <<",\n  \"contract\": \""<<xenos::resources::Sha256Hex(r.contract)<<"\""
        <<",\n  \"records\": "<<r.records<<",\n  \"unique_binaries\": "<<r.uniqueBinaries
        <<",\n  \"blocks\": "<<r.blocks<<",\n  \"failures_omitted\": "<<r.failuresOmitted
        <<",\n  \"reconstructed_hlsl_bytes_omitted\": "<<r.hlslBytesOmitted
        <<",\n  \"diagnostic_bytes_omitted\": "<<r.diagnosticBytesOmitted
        <<",\n  \"binary_bytes_before_dedup\": "<<r.binaryBytes
        <<",\n  \"binary_bytes_after_dedup\": "<<r.uniqueBinaryBytes
        <<",\n  \"compressed_payload_bytes\": "<<r.compressedBytes
        <<",\n  \"index_bytes\": "<<r.indexBytes
        <<",\n  \"file_bytes\": "<<r.fileBytes
        <<",\n  \"file_mib\": "<<std::fixed<<std::setprecision(3)<<double(r.fileBytes)/1048576.0
        <<",\n  \"all_payloads_verified\": "<<(verified?"true":"false")
        <<",\n  \"runtime_compatibility_verified\": false\n}\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"shader pack: "<<e.what()<<'\n';return 1;}
