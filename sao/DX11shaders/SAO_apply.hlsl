Texture2D<float> source;
float aoIntensity;

struct PixelInput
{
	float4 position : POSITION;
	float2 texCoords : TEXCOORD0;
};

struct PixelOutput
{
	float4 color : COLOR;
};

#define MIN_AMBIENT_LIGHT 0.1

PixelOutput ps_main(const PixelInput pixel)
{
	PixelOutput fragment;
	fragment.color = 0;

	int2 ssP = pixel.texCoords * float2(renderTargetSize[SIZECONST_WIDTH], renderTargetSize[SIZECONST_HEIGHT]);

	float ao = source.Load(int3(ssP, 0));
	ao = (clamp(1.0 - (1.0 - ao) * aoIntensity, 0.0, 1.0) + MIN_AMBIENT_LIGHT) /  (1.0 + MIN_AMBIENT_LIGHT);

	fragment.color = tex2D(colorMapSampler, pixel.texCoords) * ao;

	return fragment;
}
