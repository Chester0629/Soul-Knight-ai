#include "Util/FilledImage.hpp"

#include <algorithm>
#include <vector>

#include "Core/IndexBuffer.hpp"
#include "Core/TextureUtils.hpp"
#include "Core/VertexBuffer.hpp"

#include "Util/MissingTexture.hpp"

#include "config.hpp"

namespace Util {

FilledQuad VerticalBottomFilledQuad(float fraction) {
    const float f = std::clamp(fraction, 0.0F, 1.0F);
    const float top = -0.5F + f;  // top edge rises with f; bottom edge fixed at -0.5.
    const float vTop = 1.0F - f;  // UV.v at the top of the visible (bottom-f) region.
    FilledQuad q;
    //          TL                BL                BR               TR
    q.verts = {-0.5F, top,   -0.5F, -0.5F,    0.5F, -0.5F,    0.5F, top};
    q.uvs = {0.0F, vTop,     0.0F, 1.0F,      1.0F, 1.0F,     1.0F, vTop};
    return q;
}

namespace {
std::shared_ptr<SDL_Surface> LoadSurface(const std::string &filepath) {
    auto surface = std::shared_ptr<SDL_Surface>(IMG_Load(filepath.c_str()),
                                                SDL_FreeSurface);
    if (surface == nullptr) {
        // Load failed -> render the MissingTexture placeholder (a visible signal).
        // NO logger here: FilledImage compiles in PTSD (no C4996 suppression, unlike
        // the game targets), and spdlog's fmt formatting floods C4996 under /W4; the
        // visible placeholder is the failure signal instead.
        surface = {GetMissingImageTextureSDLSurface(), SDL_FreeSurface};
    }
    return surface;
}
} // namespace

std::unique_ptr<Core::Program> FilledImage::s_Program = nullptr;

FilledImage::FilledImage(const std::string &filepath, bool useAA) {
    if (s_Program == nullptr) {
        InitProgram();
    }
    m_UniformBuffer = std::make_unique<Core::UniformBuffer<Core::Matrices>>(
        *s_Program, "Matrices", 0);

    auto surface = LoadSurface(filepath);
    m_Texture = std::make_unique<Core::Texture>(
        Core::SdlFormatToGlFormat(surface->format->format), surface->w, surface->h,
        surface->pixels, useAA);
    m_Size = {surface->w, surface->h};
    RebuildQuad(); // m_Fraction starts at 1.0 -> full quad.
}

void FilledImage::SetFraction(float fraction) {
    const float f = std::clamp(fraction, 0.0F, 1.0F);
    if (f == m_Fraction) {
        return; // no geometry change -> keep the existing vertex array.
    }
    m_Fraction = f;
    RebuildQuad();
}

void FilledImage::RebuildQuad() {
    const FilledQuad q = VerticalBottomFilledQuad(m_Fraction);
    auto va = std::make_unique<Core::VertexArray>();
    va->AddVertexBuffer(std::make_unique<Core::VertexBuffer>(
        std::vector<float>(q.verts.begin(), q.verts.end()), 2));
    va->AddVertexBuffer(std::make_unique<Core::VertexBuffer>(
        std::vector<float>(q.uvs.begin(), q.uvs.end()), 2));
    va->SetIndexBuffer(std::make_unique<Core::IndexBuffer>(
        std::vector<unsigned int>{0, 1, 2, 0, 2, 3}));
    m_VertexArray = std::move(va);
}

void FilledImage::Draw(const Core::Matrices &data) {
    if (m_Fraction <= 0.0F || m_VertexArray == nullptr) {
        return; // fraction 0 -> nothing to draw (degenerate quad).
    }
    m_UniformBuffer->SetData(0, data);

    m_Texture->Bind(UNIFORM_SURFACE_LOCATION);
    s_Program->Bind();
    s_Program->Validate();

    m_VertexArray->Bind();
    m_VertexArray->DrawTriangles();
}

void FilledImage::InitProgram() {
    s_Program =
        std::make_unique<Core::Program>(PTSD_ASSETS_DIR "/shaders/Base.vert",
                                        PTSD_ASSETS_DIR "/shaders/Base.frag");
    s_Program->Bind();
    GLint location = glGetUniformLocation(s_Program->GetId(), "surface");
    glUniform1i(location, UNIFORM_SURFACE_LOCATION);
}

} // namespace Util
