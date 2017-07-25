/**
 \file SAO.h
 \author Morgan McGuire and Michael Mara, NVIDIA Research

 Implementation of:
 
 Scalable Ambient Obscurance.
 Morgan McGuire, Michael Mara, and David Luebke, <i>HPG</i> 2012 
 
 SAO is an optimized variation of the "Alchemy AO" screen-space ambient obscurance algorithm. It is 3x-7x faster on NVIDIA GPUs 
 and easier to integrate than the original algorithm. The mathematical ideas were
 first described in McGuire, Osman, Bukowski, and Hennessy, The Alchemy Screen-Space Ambient Obscurance Algorithm, <i>HPG</i> 2011
 and were developed at Vicarious Visions.  

  Open Source under the "BSD" license: http://www.opensource.org/licenses/bsd-license.php

  Copyright (c) 2011-2012, NVIDIA
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
  Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */
#ifndef SAO_h
#define SAO_h

// Uses the G3D library (http://g3d.sf.net) as a light wrapper around OpenGL
// to avoid boilerplate.
#include <G3D/G3DAll.h>

/**
 \brief Screen-space ambient obscurance.
 
 <h3>Initialization</h3>

    \code
    SAO::Ref         sao;
    Texture::Ref     aoBuffer;
    Framebuffer::Ref aoResultFramebuffer;
 
    sao = SAO::create();

    aoBuffer = Texture::createEmpty("aoBuffer", width, height, ImageFormat::R8(), Texture::DIM_2D_NPOT, Texture::Settings::buffer());

    aoResultFramebuffer = Framebuffer::create("aoResultFramebuffer");
    aoResultFramebuffer->set(Framebuffer::COLOR0, aoBuffer);
    \endcode

 <h3>Per Frame</h3>
    \code
    rd->push2D(aoResultFramebuffer); {
        sao->compute(rd, depthBuffer, camera);
    } rd->pop2D();
    \endcode

  After rendering, bind <code>aoBuffer</code> in the shading pass and use it to modulate
  ambient illumination.

 \author Morgan McGuire and Michael Mara, NVIDIA and Williams College, http://research.nvidia.com, http://graphics.cs.williams.edu 
*/
class SAO : public ReferenceCountedObject {
public:
    /** Provide automated resource management. Use SAO::Ref in place of SAO* and never call delete. */
    typedef ReferenceCountedPointer<class SAO> Ref;

protected:
    
    class Settings {
    public:
        /** Radius in world-space units */
        float                       radius;

        /** 
          Increase if you have low-poly curves that are getting too
          much self-shadowing in shallow corners.  Decrease if you see white 
          lines in sharp corners.

          Bias addresses two problems.  The first is that a
          tessellated concave surface should geometrically exhibit
          stronger occlusion near edges and vertices, but this is
          often undesirable if the surface is supposed to appear as a
          smooth curve.  Increasing bias increases the maximum
          concavity that can occur before AO begins.

          The second is that due to limited precision in the depth
          buffer, a surface could appear to occlude itself.
        */
        float                       bias;

        float                       intensity;

        Settings();
    };                       

    Settings                        m_settings;

    /** Stores camera-space (negative) linear z values at various scales in the MIP levels */
    Texture::Ref                    m_cszBuffer;
    Shader::Ref                     m_reconstructCSZShader;
    // buffer[i] is used for MIP level i
    Array<Framebuffer::Ref>         m_cszFramebuffers;
    Shader::Ref                     m_cszMinifyShader;

    /** Has AO in R and depth in G * 256 + B.*/
    Texture::Ref                    m_rawAOBuffer;
    Framebuffer::Ref                m_rawAOFramebuffer;
    Shader::Ref                     m_rawAOShader;

    /** Has AO in R and depth in G */
    Texture::Ref                    m_hBlurredBuffer;
    Framebuffer::Ref                m_hBlurredFramebuffer;
    Shader::Ref                     m_blurShader;

    /** \param width Total buffer size of the GBuffer, including the guard band */
    void resizeBuffers(int width, int height);

    void computeCSZ
       (RenderDevice* rd,         
        const Texture::Ref&         depthBuffer, 
        const Vector3&              clipInfo);

    void computeRawAO
       (RenderDevice* rd,         
        const Texture::Ref&         depthBuffer, 
        const Vector3&              clipConstant,
        const Vector4&              projConstant,
        float                       projScale,
        const Texture::Ref&         csZBuffer,
        const int                   guardBandSize);

    void blurHorizontal
        (RenderDevice*              rd, 
        const Texture::Ref&         depthBuffer,
        const int                   guardBandSize);

    void blurVertical
        (RenderDevice*              rd, 
        const Texture::Ref&         depthBuffer,
        const int                   guardBandSize);

public:

    /** \brief Create a new SAO instance. 
    
        Only one is ever needed, but if you are rendering to differently-sized
        framebuffers it is faster to create one instance per resolution than to
        constantly force SAO to resize its internal buffers. */
    static Ref create();
    
    /**
     \brief Render the obscurance constant at each pixel to the currently-bound framebuffer.

     \param rd The rendering device/graphics context.  The currently-bound framebuffer must
     match the dimensions of \a depthBuffer.

     \param radius Maximum obscurance radius in world-space scene units (e.g., meters, inches)

     \param depthBuffer Standard hyperbolic depth buffer.  Can
      be from either an infinite or finite far plane
      depending on the values in projConstant and clipConstant.

     \param clipConstant Constants based on clipping planes:
     \code
        const double width  = rd->width();
        const double height = rd->height();
        const double z_f    = camera.farPlaneZ();
        const double z_n    = camera.nearPlaneZ();

        const Vector3& clipConstant = 
            (z_f == -inf()) ? 
                Vector3(float(z_n), -1.0f, 1.0f) : 
                Vector3(float(z_n * z_f),  float(z_n - z_f),  float(z_f));
     \endcode

     \param projConstant Constants based on the projection matrix:
     \code
        Matrix4 P;
        camera.getProjectUnitMatrix(rd->viewport(), P);
        const Vector4 projConstant
            (float(-2.0 / (width * P[0][0])), 
             float(-2.0 / (height * P[1][1])),
             float((1.0 - (double)P[0][2]) / P[0][0]), 
             float((1.0 + (double)P[1][2]) / P[1][1]));
     \endcode

     \param projScale Pixels-per-meter at z=-1, e.g., computed by:
     
      \code
       -height / (2.0 * tan(verticalFieldOfView * 0.5))
      \endcode

     This is usually around 500.


     \param guardBandSize Size on EACH SIDE of the depthBuffer and output target that should be ignored when computing AO
     */
    void compute
       (RenderDevice*               rd,
        const Texture::Ref&         depthBuffer, 
        const Vector3&              clipConstant,
        const Vector4&              projConstant,
        float                       projScale,
        const int                   guardBandSize = 0);

    /** \brief Convenience wrapper for the full version of compute() when
        using only a depth buffer. 

        \param camera The camera that the scene was rendered with.
    */
    void compute
       (RenderDevice*               rd,
        const Texture::Ref&         depthBuffer, 
        const GCamera&              camera,
        const int                   guardBandSize = 0);

    /** For debugging; not needed to be called from outside of SAO in production code */
    void reloadShaders();

    /** Increase to compute AO from more distant objects, at a performance and image quality cost. Default is 0.20 meters. */
    void setRadius(float r) {
        alwaysAssertM(r > 0, "Radius must be non-negative");
        m_settings.radius = r;
    }

    float radius() const {
        return m_settings.radius;
    }
    
    /** Increase to avoid self-shadowing in mesh corners, decrease to improve AO for small features and eliminate white halos. 
        Default is 0.012m. \copydoc SAO::Settings::bias*/
    void setBias(float b) {
        m_settings.bias = b;
    }

    float bias() const {
        return m_settings.bias;
    }

    /** Darkness multiplier */
    void setIntensity(float d) {
        m_settings.intensity = d;
    }

    float intensity() const {
        return m_settings.intensity;
    }

};

#endif // SAO_h
