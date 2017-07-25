/**
 \file SAO.cpp
 \author Morgan McGuire and Michael Mara, NVIDIA and Williams College, http://research.nvidia.com, http://graphics.cs.williams.edu

  Open Source under the "BSD" license: http://www.opensource.org/licenses/bsd-license.php

  Copyright (c) 2011-2012, NVIDIA
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
  Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */
#include "SAO.h"

/** Floating point bits per pixel for CSZ: 16 or 32.  There is no perf difference on GeForce GTX 580 */
#define ZBITS (32)

/** This must be greater than or equal to the MAX_MIP_LEVEL and  defined in SAO_AO.pix. */
#define MAX_MIP_LEVEL (5)

/** Used to allow us to depth test versus the sky without an explicit check, speeds up rendering when some of the skybox is visible */
#define Z_COORD (-1.0f)

SAO::Settings::Settings() : 
    radius(1.0f * units::meters()),
    bias(0.012f),
    intensity(1.0f) {}


SAO::Ref SAO::create() {
    return new SAO();
}


void SAO::compute
   (RenderDevice*               rd,
    const Texture::Ref&         depthBuffer, 
    const Vector3&              clipConstant,
    const Vector4&              projConstant,
    float                       projScale,
    const int                   guardBandSize) {

    alwaysAssertM(depthBuffer.notNull(), 
        "Depth buffer is required.");

    if (m_blurShader.isNull()) {
        reloadShaders();
    }

    resizeBuffers(depthBuffer->width(), depthBuffer->height());

    computeCSZ(rd, depthBuffer, clipConstant);

    computeRawAO(rd, depthBuffer, clipConstant, projConstant, projScale, m_cszBuffer, guardBandSize);

    blurHorizontal(rd, depthBuffer, guardBandSize);

    blurVertical(rd, depthBuffer, guardBandSize);
}


void SAO::reloadShaders() {
    m_rawAOShader = Shader::fromFiles(System::findDataFile("SAO.vrt"), System::findDataFile("SAO_AO.pix"));
    m_rawAOShader->setPreserveState(false);

    m_blurShader = Shader::fromFiles(System::findDataFile("SAO.vrt"), System::findDataFile("SAO_blur.pix"));
    m_blurShader->setPreserveState(false);

    m_reconstructCSZShader = Shader::fromFiles(System::findDataFile("SAO.vrt"), System::findDataFile("SAO_reconstructCSZ.pix"));
    m_reconstructCSZShader->setPreserveState(false);

    m_cszMinifyShader = Shader::fromFiles(System::findDataFile("SAO.vrt"), System::findDataFile("SAO_minify.pix"));
    m_cszMinifyShader->setPreserveState(false);
}


void SAO::resizeBuffers(int width, int height) {
    bool rebind = false;

    if (m_rawAOFramebuffer.isNull()) {
        // Allocate for the first call
        m_rawAOFramebuffer    = Framebuffer::create("rawAOFramebuffer");
        m_hBlurredFramebuffer = Framebuffer::create("hBlurredFramebuffer");

        m_rawAOBuffer         = Texture::createEmpty("rawAOBuffer",    width, height, ImageFormat::RGB8(), Texture::DIM_2D_NPOT, Texture::Settings::buffer());
        m_hBlurredBuffer      = Texture::createEmpty("hBlurredBuffer", width, height, ImageFormat::RGB8(), Texture::DIM_2D_NPOT, Texture::Settings::buffer());

        // R16F is too low-precision, but we provide it as a fallback
        const ImageFormat* csZFormat =
            (ZBITS == 16) ? 

            (GLCaps::supportsTextureDrawBuffer(ImageFormat::R16F()) ? ImageFormat::R16F() : ImageFormat::L16F()) :

            (GLCaps::supportsTextureDrawBuffer(ImageFormat::R32F()) ? ImageFormat::R32F() : 
             (GLCaps::supportsTextureDrawBuffer(ImageFormat::L32F()) ? ImageFormat::L32F() :
              ImageFormat::RG32F()));
        alwaysAssertM(ZBITS == 16 || ZBITS == 32, "Only ZBITS = 16 and 32 are supported.");

        Texture::Settings cszSettings = Texture::Settings::buffer();
        cszSettings.interpolateMode   = Texture::NEAREST_MIPMAP;
        cszSettings.maxMipMap         = MAX_MIP_LEVEL;
        debugAssert(width > 0 && height > 0);
        m_cszBuffer                   = Texture::createEmpty("cszBuffer", width, height, csZFormat, Texture::DIM_2D_NPOT, cszSettings);

        for (int i = 0; i <= MAX_MIP_LEVEL; ++i) {
            m_cszFramebuffers.append(Framebuffer::create("cszFramebuffers[" + G3D::format("%d", i) + "]"));
        }

        rebind = true;

    } else if ((m_rawAOBuffer->width() != width) || (m_rawAOBuffer->height() != height)) {
        // Resize
        m_rawAOBuffer->resize(width, height);
        m_hBlurredBuffer->resize(width, height);
        m_cszBuffer->resize(width, height);

        rebind = true;
    }

    if (rebind) {
        // Sizes have changed or just been allocated
        m_rawAOFramebuffer->set(Framebuffer::COLOR0, m_rawAOBuffer);
        m_hBlurredFramebuffer->set(Framebuffer::COLOR0, m_hBlurredBuffer);

        for (int i = 0; i <= MAX_MIP_LEVEL; ++i) {
            m_cszFramebuffers[i]->set(Framebuffer::COLOR0, m_cszBuffer, CubeFace::POS_X, i);
        }
    }
}


void SAO::computeCSZ
(RenderDevice* rd,         
 const Texture::Ref&         depthBuffer, 
 const Vector3&              clipInfo) {

    // Generate level 0
    rd->push2D(m_cszFramebuffers[0]); {
        rd->clear();
        m_reconstructCSZShader->args.set("clipInfo",                 clipInfo);
        m_reconstructCSZShader->args.set("DEPTH_AND_STENCIL_buffer", depthBuffer);
        rd->applyRect(m_reconstructCSZShader);
    } rd->pop2D();


    // Generate the other levels
    for (int i = 1; i <= MAX_MIP_LEVEL; ++i) {
        m_cszMinifyShader->args.set("texture", m_cszBuffer);
        rd->push2D(m_cszFramebuffers[i]); {
            rd->clear();
            m_cszMinifyShader->args.set("previousMIPNumber", i - 1);
            rd->applyRect(m_cszMinifyShader);
        } rd->pop2D();
    }
}


void SAO::computeRawAO
   (RenderDevice*               rd,
    const Texture::Ref&         depthBuffer, 
    const Vector3&              clipConstant,
    const Vector4&              projConstant,
    const float                 projScale,
    const Texture::Ref&         csZBuffer,
    const int                   guardBandSize) {

    debugAssert(projScale > 0);
    m_rawAOFramebuffer->set(Framebuffer::DEPTH,      depthBuffer);
    rd->push2D(m_rawAOFramebuffer); {

        // For quick early-out testing vs. skybox 
        rd->setDepthTest(RenderDevice::DEPTH_GREATER);

        // Values that are never touched due to the depth test will be white
        rd->setColorClearValue(Color3::white());
        rd->clear(true, false, false);
        Shader::ArgList& args = m_rawAOShader->args;

        args.set("radius",      m_settings.radius);
        args.set("bias",        m_settings.bias);
        args.set("clipInfo",    clipConstant);
        args.set("projInfo",    projConstant);
        args.set("projScale",   projScale);
        args.set("CS_Z_buffer", csZBuffer);
        args.set("intensityDivR6", m_settings.intensity / pow(m_settings.radius, 6.0f));

        rd->setClip2D(Rect2D::xyxy(guardBandSize, guardBandSize, rd->viewport().width() - guardBandSize, rd->viewport().height() - guardBandSize));
       
        rd->applyRect(m_rawAOShader, Z_COORD);
    } rd->pop2D();
}


void SAO::blurHorizontal
   (RenderDevice*               rd,
    const Texture::Ref&         depthBuffer, 
    const int                   guardBandSize) {

    rd->push2D(m_hBlurredFramebuffer); {
        rd->setColorClearValue(Color3::white());
        rd->clear(true, false, false);

        m_blurShader->args.set("source",                    m_rawAOBuffer);
        m_blurShader->args.set("axis",                      Vector2int16(1, 0));

        rd->setClip2D(Rect2D::xyxy(guardBandSize, guardBandSize, rd->viewport().width() - guardBandSize, rd->viewport().height() - guardBandSize));
       
        rd->applyRect(m_blurShader, Z_COORD);
    } rd->pop2D();
}


void SAO::blurVertical   
   (RenderDevice*               rd,
    const Texture::Ref&         depthBuffer, 
    const int                   guardBandSize) {

    // Render directly to the currently-bound framebuffer
    rd->push2D(); {
        rd->setColorClearValue(Color3::white());
        rd->clear(true, false, false);

        m_blurShader->args.set("source",                    m_hBlurredBuffer);
        m_blurShader->args.set("axis",                      Vector2int16(0, 1));

        rd->setClip2D(Rect2D::xyxy(guardBandSize, guardBandSize, rd->viewport().width() - guardBandSize, rd->viewport().height() - guardBandSize));
       
        rd->applyRect(m_blurShader, Z_COORD);
    } rd->pop2D();
}


void SAO::compute
   (RenderDevice*               rd,
    const Texture::Ref&         depthBuffer, 
    const GCamera&              camera,
    const int                   guardBandSize) {

    const double width  = depthBuffer->width();
    const double height = depthBuffer->height();
    const double z_f    = camera.farPlaneZ();
    const double z_n    = camera.nearPlaneZ();

    const Vector3& clipConstant = 
        (z_f == -inf()) ? 
            Vector3(float(z_n), -1.0f, 1.0f) : 
            Vector3(float(z_n * z_f),  float(z_n - z_f),  float(z_f));

    Matrix4 P;
    camera.getProjectUnitMatrix(Rect2D::xywh(0, 0, width, height), P);
    const Vector4 projConstant
        (float(-2.0 / (width * P[0][0])), 
         float(-2.0 / (height * P[1][1])),
         float((1.0 - (double)P[0][2]) / P[0][0]), 
         float((1.0 + (double)P[1][2]) / P[1][1]));

    compute(rd, depthBuffer, clipConstant, projConstant, abs(camera.imagePlanePixelsPerMeter(rd->viewport())), guardBandSize);
}
