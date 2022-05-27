#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <zfp.h>
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>

const std::string USAGE = R"(Usage:
To compress a raw volume:
./zfp_make_test_data -raw (volume_XxYxZx_dtype.raw) -crate (compression_rate)

To generate a data set and compress it:
./zfp_make_test_data -gen (plane_x|quarter_sphere|sphere|wavelet) -dims (x y z) -crate (compression_rate)

Shared Options:

    -crate (compression_rate)         Specify the compression rate to use for the volume. Must be an
                                      an integer from [1-32]. This specifies the target bits per value
                                      in the output stream. 1 = one bit per value, 32 = 32 bits per value.
                                      All data sets are expanded to floats, so 32 means no compression. 

    -h                                Show this help.

In raw volume compress mode:

    -raw (volume_XxYxZx_dtype.raw)    Specify the raw volume to load and compress. Volumes must be
                                      named following the convention used on OpenSciVisData sets:
                                      <volume_name>_<X>x<Y>x<Z>_<data type>.raw.

In generated volume compress mode:

    -gen (plane_x|quarter_sphere|sphere|wavelet)
                                      Specify the type of volume field to generate.

    -dims (x y z)                     Specify the grid dimensions of the generated volume.
)";

bool read_raw_volume(const std::string &raw_file_name,
                     std::vector<float> &data,
                     glm::uvec3 &dims);

bool generate_volume(const std::string &gen_mode_name,
                     const glm::uvec3 &gen_dims,
                     std::vector<float> &data);

int main(int argc, char **argv)
{
    using namespace std::chrono;
    std::vector<std::string> args(argv + 1, argv + argc);

    if (std::find(args.begin(), args.end(), std::string("-h")) != args.end()) {
        std::cout << USAGE << "\n";
        return 0;
    }

    bool raw_volume_mode = false;
    bool gen_volume_mode = false;
    int compression_rate = -1;
    std::string raw_file_name;
    std::string gen_mode_name;
    glm::uvec3 gen_dims(0);
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-crate") {
            compression_rate = std::stoi(args[++i]);
        } else if (args[i] == "-raw") {
            raw_volume_mode = true;
            raw_file_name = args[++i];
        } else if (args[i] == "-gen") {
            gen_volume_mode = true;
            gen_mode_name = args[++i];
        } else if (args[i] == "-dims") {
            gen_dims.x = std::stoul(args[++i]);
            gen_dims.y = std::stoul(args[++i]);
            gen_dims.z = std::stoul(args[++i]);
        } else {
            std::cout << "Unrecognized argument " << args[i] << "\n";
            return 1;
        }
    }

    if (!raw_volume_mode && !gen_volume_mode) {
        std::cout << "A mode -raw or -gen is required.\n" << USAGE << "\n";
        return 1;
    }
    if (raw_volume_mode && gen_volume_mode) {
        std::cout << "Only one mode -raw or -gen may be passed\n" << USAGE << "\n";
        return 1;
    }
    if (gen_volume_mode && gen_dims == glm::uvec3(0)) {
        std::cout << "Generated mode requires volume dims to generate\n" << USAGE << "\n";
        return 1;
    }

    std::string out_name;
    std::vector<float> volume_data;
    glm::uvec3 volume_dims(0);
    if (raw_volume_mode) {
        if (!read_raw_volume(raw_file_name, volume_data, volume_dims)) {
            std::cout << "Failed to read raw volume " << raw_file_name << "\n";
            return 1;
        }
        out_name = raw_file_name;
    } else {
        if (!generate_volume(gen_mode_name, gen_dims, volume_data)) {
            std::cout << "Failed to generate volume\n";
            return 1;
        }
        volume_dims = gen_dims;
        out_name = gen_mode_name + "_" + std::to_string(volume_dims.x) + "x" +
                   std::to_string(volume_dims.y) + "x" + std::to_string(volume_dims.z) +
                   "_float32.gen";
    }

    std::cout << "Uncompressed size: " << volume_data.size() * sizeof(float) << "b\n";

    zfp_stream *zfp = zfp_stream_open(nullptr);
    float used_compression_rate =
        zfp_stream_set_rate(zfp, compression_rate, zfp_type_float, 3, 0);
    std::cout << "Used compression rate: " << used_compression_rate << "\n";
    if (std::floor(used_compression_rate) != used_compression_rate) {
        std::cout << "Error: non-integer compression rate\n";
        return 1;
    }

    // Just compress the first block. This is also not really the first proper
    // block of 4^3 voxels but just the first 64 voxels in the data, but ok for testing
    zfp_field *field = zfp_field_3d(
        volume_data.data(), zfp_type_float, volume_dims.x, volume_dims.y, volume_dims.z);

    const size_t bufsize = zfp_stream_maximum_size(zfp, field);

    std::vector<uint8_t> compressed_data(bufsize, 0);
    bitstream *stream = stream_open(compressed_data.data(), compressed_data.size());
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    size_t total_bytes = zfp_compress(zfp, field);
    zfp_field_free(field);

    compressed_data.resize(total_bytes);
    std::cout << "Total compressed size: " << compressed_data.size() << "B\n";

    stream_close(stream);
    zfp_stream_close(zfp);

    // Save out the compressed file
    out_name = out_name + ".crate" + std::to_string(int(used_compression_rate)) + ".zfp";
    std::ofstream out_file(out_name.c_str(), std::ios::binary);
    out_file.write(reinterpret_cast<const char *>(compressed_data.data()),
                   compressed_data.size());

    return 0;
}

bool read_raw_volume(const std::string &raw_file_name,
                     std::vector<float> &data,
                     glm::uvec3 &dims)
{
    const std::regex match_filename("(\\w+)_(\\d+)x(\\d+)x(\\d+)_(.+)\\.raw");
    auto matches =
        std::sregex_iterator(raw_file_name.begin(), raw_file_name.end(), match_filename);
    if (matches == std::sregex_iterator() || matches->size() != 6) {
        std::cerr << "Unrecognized raw volume naming scheme, expected a format like: "
                  << "'<name>_<X>x<Y>x<Z>_<data type>.raw' but '" << raw_file_name
                  << "' did not match" << std::endl;
        return false;
    }

    dims = glm::uvec3(
        std::stoi((*matches)[2]), std::stoi((*matches)[3]), std::stoi((*matches)[4]));
    const std::string volume_type = (*matches)[5];

    size_t voxel_size = 0;
    if (volume_type == "uint8") {
        voxel_size = 1;
    } else if (volume_type == "uint16") {
        voxel_size = 2;
    } else if (volume_type == "float32") {
        voxel_size = 4;
    }

    const size_t num_voxels = size_t(dims.x) * size_t(dims.y) * size_t(dims.z);
    data.resize(num_voxels, 0.f);
    {
        std::ifstream fin(raw_file_name.c_str(), std::ios::binary);
        if (volume_type != "float32") {
            std::vector<uint8_t> read_data(num_voxels * voxel_size, 0);
            fin.read(reinterpret_cast<char *>(read_data.data()), read_data.size());
            if (volume_type == "uint8") {
                for (size_t i = 0; i < num_voxels; ++i) {
                    data[i] = read_data[i];
                }
            } else if (volume_type == "uint16") {
                uint16_t *d = reinterpret_cast<uint16_t *>(read_data.data());
                for (size_t i = 0; i < num_voxels; ++i) {
                    data[i] = d[i];
                }
            }
        } else {
            fin.read(reinterpret_cast<char *>(data.data()), data.size());
        }
    }
    return true;
}

bool generate_volume(const std::string &gen_mode_name,
                     const glm::uvec3 &gen_dims,
                     std::vector<float> &data)
{
    data.resize(size_t(gen_dims.x) * size_t(gen_dims.y) * size_t(gen_dims.z), 0.f);
    if (gen_mode_name == "plane_x") {
        // Generate a plane field increasing along x by just filling voxels with their
        // normalized x coordinate
        std::cout << "Generating plane_x volume, size: " << glm::to_string(gen_dims) << "\n";
        for (size_t z = 0; z < gen_dims.z; ++z) {
            for (size_t y = 0; y < gen_dims.y; ++y) {
                for (size_t x = 0; x < gen_dims.x; ++x) {
                    const size_t voxel = x + gen_dims.x * (y + gen_dims.y * z);
                    data[voxel] = static_cast<float>(x) / gen_dims.x;
                }
            }
        }
        return true;
    } else if (gen_mode_name == "quarter_sphere") {
        // Generate a quarter sphere field by just looking at distance from the origin of the
        // volume
        std::cout << "Generating sphere volume, size: " << glm::to_string(gen_dims) << "\n";
        const float max_dist = glm::length(glm::vec3(gen_dims));
        for (size_t z = 0; z < gen_dims.z; ++z) {
            for (size_t y = 0; y < gen_dims.y; ++y) {
                for (size_t x = 0; x < gen_dims.x; ++x) {
                    const size_t voxel = x + gen_dims.x * (y + gen_dims.y * z);
                    const float dist = glm::length(glm::vec3(x, y, z));
                    data[voxel] = dist / max_dist;
                }
            }
        }
        return true;
    } else if (gen_mode_name == "sphere") {
        // Generate a sphere field with the origin of the sphere in the middle of the volume
        std::cout << "Generating sphere volume, size: " << glm::to_string(gen_dims) << "\n";
        const glm::vec3 sphere_origin(gen_dims.x / 2.f, gen_dims.y / 2.f, gen_dims.z / 2.f);
        const float max_dist = gen_dims.x / 2.f;
        for (size_t z = 0; z < gen_dims.z; ++z) {
            for (size_t y = 0; y < gen_dims.y; ++y) {
                for (size_t x = 0; x < gen_dims.x; ++x) {
                    const size_t voxel = x + gen_dims.x * (y + gen_dims.y * z);
                    const float dist = glm::length(glm::vec3(x, y, z) - sphere_origin);
                    data[voxel] = dist / max_dist;
                }
            }
        }
        return true;

    } else if (gen_mode_name == "wavelet") {
        // Generate the wavelet test volume, borrowed from OpenVKL
        // https://github.com/openvkl/openvkl/blob/ec551ea08cbceab187326e2358fdc1ceeffaf1d6/testing/volume/procedural_functions.h#L39-L61
        std::cout << "Generating wavelet volume, size: " << glm::to_string(gen_dims) << "\n";
        // wavelet parameters
        constexpr float M = 1.f;
        constexpr float G = 1.f;
        constexpr float XM = 1.f;
        constexpr float YM = 1.f;
        constexpr float ZM = 1.f;
        constexpr float XF = 3.f;
        constexpr float YF = 3.f;
        constexpr float ZF = 3.f;

        for (size_t z = 0; z < gen_dims.z; ++z) {
            for (size_t y = 0; y < gen_dims.y; ++y) {
                for (size_t x = 0; x < gen_dims.x; ++x) {
                    const size_t voxel = x + gen_dims.x * (y + gen_dims.y * z);
                    const glm::vec3 coords =
                        2.f * (glm::vec3(x, y, z) / glm::vec3(gen_dims)) - glm::vec3(1.f);
                    const float value =
                        M * G *
                        (XM * std::sin(XF * coords.x) + YM * std::sin(YF * coords.y) +
                         ZM * std::cos(ZF * coords.z));
                    data[voxel] = value;
                }
            }
        }
        return true;
    }
    std::cout << "Unrecognized/unimplemented generation mode " << gen_mode_name << "\n";
    return false;
}
