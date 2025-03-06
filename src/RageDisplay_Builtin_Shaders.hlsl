#define TEXTURE_SHIFT 2u
#define TEXTURE_MASK ((1u << TEXTURE_SHIFT) - 1u)

#define TEXTURE_MODE_MODULATE 0u
#define TEXTURE_MODE_GLOW 1u
#define TEXTURE_MODE_ADD 2u

struct VertexData
{
	float3 position : SV_Position;
	float3 normal : NORMAL;
#if VERTEX_HAS_COLOR
	uint color : COLOR; // For compatibility with legacy D3D renderer, DWORD in ARGB order
#endif
	float2 texcoord : TEXCOORD;
#if VERTEX_HAS_TEXTURE_MATRIX_SCALE
	float2 textureMatrixScale : TEXCOORD1;
#endif
};

struct FragmentData
{
	float4 position : SV_Position;
	float4 color : COLOR;
	float4 texcoord : TEXCOORD;
};

struct LightData
{
	float4 ambient;
	float4 diffuse;
	float4 specular;
	float3 direction;
};

cbuffer ConstantsVS : register(b0)
{
	float4x4 vertexTransform;
	float3x3 normalTransform;
	float4x4 texcoordTransform;
	uint numLights;
	float materialShininess;
	float4 noLightingMaterialColor;
	float4 materialAmbient;
	float4 materialDiffuse;
	float4 materialSpecular;
	float4 materialEmission;
	LightData lights[MAX_LIGHTS];
};

// numLights == 0 means that lighting is disabled, otherwise lighting is enabled but the number of lights is numLights - 1
#define LIGHTING_ENABLED (numLights > 0u)
#define NUM_LIGHTS (numLights - 1u)

FragmentData VSMain(VertexData vertexData)
{
	FragmentData fragmentData;
	fragmentData.position = mul(vertexTransform, float4(vertexData.position, 1.f));

	float4 lightingAmbient;
	float4 lightingDiffuse = float4(0.f, 0.f, 0.f, 0.f);
	float4 lightingSpecular = float4(0.f, 0.f, 0.f, 0.f);

	if (LIGHTING_ENABLED)
	{
		lightingAmbient = float4(0.f, 0.f, 0.f, 0.f);
		const float3 normal = normalize(mul(normalTransform, vertexData.normal));

		for (uint i = 0u; i < NUM_LIGHTS; ++i)
		{
			const float3 dir = -lights[i].direction;
			// Since we are in eye coordinaties, (0, 0, 1) is the view direction
			const float3 halfDir = normalize(dir + float3(0.f, 0.f, 1.f));

			lightingAmbient += lights[i].ambient;
			lightingDiffuse += lights[i].diffuse * saturate(dot(normal, dir));
			lightingSpecular += lights[i].specular * pow(saturate(dot(normal, halfDir)), materialShininess);
		}

		lightingAmbient = (lightingAmbient * materialAmbient) + materialEmission;
		lightingDiffuse *= materialDiffuse;
		lightingSpecular *= materialSpecular;
	}
	else
		lightingAmbient = noLightingMaterialColor;

#if VERTEX_HAS_COLOR
	const float4 vertexColor = (uint4(vertexData.color >> 16u, vertexData.color >> 8u, vertexData.color, vertexData.color >> 24u) & 0xffu) / 255.f;
#else
	const float4 vertexColor = float4(1.f, 1.f, 1.f, 1.f);
#endif

	// TODO should we include lighting alpha in the calculations?
	fragmentData.color = vertexColor * float4((lightingAmbient + lightingDiffuse + lightingSpecular).rgb, 1.f);

	const float4 texcoord = float4(vertexData.texcoord, 0.f, 1.f);
	const float4 transformedTexcoord = mul(texcoordTransform, texcoord);
#if VERTEX_HAS_TEXTURE_MATRIX_SCALE
	fragmentData.texcoord = lerp(texcoord, transformedTexcoord, float4(vertexData.textureMatrixScale, 1.f, 1.f));
#else
	fragmentData.texcoord = transformedTexcoord;
#endif

	return fragmentData;
}

cbuffer ConstantsPS : register(b0)
{
	uint numTextures;
	uint textureModes;
	bool bAlphaTestEnabled;
};

Texture2D texture0 : register(t0);
Texture2D texture1 : register(t1);
Texture2D texture2 : register(t2);
Texture2D texture3 : register(t3);

SamplerState sampler0 : register(s0);
SamplerState sampler1 : register(s1);
SamplerState sampler2 : register(s2);
SamplerState sampler3 : register(s3);

static const Texture2D textures[MAX_TEXTURES] = { texture0, texture1, texture2, texture3 };
static const SamplerState samplers[MAX_TEXTURES] = { sampler0, sampler1, sampler2, sampler3 };

float4 PSMain(FragmentData fragmentData) : SV_Target
{
	float4 color = fragmentData.color;
	const float2 transformedTexcoord = fragmentData.texcoord.xy / fragmentData.texcoord.w;

	[unroll(MAX_TEXTURES)]
	for (uint i = 0u; i < numTextures; ++i)
	{
		const uint textureMode = (textureModes >> (i * TEXTURE_SHIFT)) & TEXTURE_MASK;
		const float4 textureColor = textures[i].Sample(samplers[i], transformedTexcoord);
		switch (textureMode)
		{
			case TEXTURE_MODE_MODULATE:
				color *= textureColor;
				break;
			case TEXTURE_MODE_GLOW:
				color.a *= textureColor.a;
				break;
			case TEXTURE_MODE_ADD:
				color.rgb += textureColor.rgb;
				color.a *= textureColor.a;
				break;
		}
	}

	if (bAlphaTestEnabled && color.a <= 1.f / 256.f)
		discard;

	return color;
}
