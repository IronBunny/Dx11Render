

The AO effect is implemented entirely in the SSAO* files.  The rest are just demo framework.  
The only input is a regular z buffer, and the output is a full-screen AO texture that you 
use during lighting to modulate environment/ambient lighting.

The effect consists of the following passes:



1. Depth -> linear Z (SSAO_reconstructCSZ.pix)

2. Z -> Z Mipmaps (SSAO_minify.pix)
3

. Noisy AO (SSAO_AO.pix)

4. Blur horizontal (SSAO_blur.pix)

5. Blur vertical (SSAO_blur.pix)



SSAO.cpp shows how to compute the needed projection matrix constants on the host.  This code 
was written against the G3D Innovation Engine 9.0 infrastructure from http://g3d.sf.net.  That infrastructure is just used for the demo--there's nothing G3D or OpenGL-specific about 
the effect. 


The graininess of the AO for small radius samples is due to the blur pass skipping to every third pixel.  A more refined blur will cost a little more but gives smoother results.  I recommend a better blur
if you're already using blurring for screen-space soft shadows, bloom, or another effect that
can be blurred at the same time in other channels of the AO texture.

 The net timing results 
are about 15% higher than optimal because of the demo infrastructure--the overhead for launching a shading pass is large because of the way that we structured the code in G3D, 
but the cost per pixel should be close to optimal.

To recompile from source, you will need 
the G3D Innovation Engine source as of June 20, 2012, which can be downloaded from:



svn co -r {2012-06-20} https://g3d.svn.sourceforge.net/svnroot/g3d g3d
