#include "platform/video/deko3d/dksh_shader_source.hpp"

#include <format>
#include <utility>

#include "util/text_format.hpp"

namespace akira::platform::deko3d
{

DkshShaderSource::DkshShaderSource(std::string directory)
    : m_directory(std::move(directory))
{
}

std::optional<ShaderSource::Code> DkshShaderSource::load(StaticShader shader)
{
    return read(akira::text::format("{}.dksh", stem(shader)));
}

std::optional<ShaderSource::Code> DkshShaderSource::load(const VideoFragmentVariant& variant)
{
    return read(fileName(variant));
}

std::string DkshShaderSource::fileName(const VideoFragmentVariant& variant)
{
    std::string name = "video_fsh";
    if (variant.dithering)
        name += "_dither";
    if (variant.fsrEasu)
        name += "_easu";
    if (variant.fsrRcas)
        name += "_rcas";
    if (variant.fsrPass)
        name += "_pass";
    return name + ".dksh";
}

std::optional<ShaderSource::Code> DkshShaderSource::read(std::string_view fileName) const
{
    auto contents = readFile(akira::text::format("{}/{}", m_directory, fileName));
    if (!contents)
        return std::nullopt;
    return Code(contents->begin(), contents->end());
}

}
