#define TEXTURE_SHIFT 3u
#define TEXTURE_MODE_MASK 3u
#define TEXTURE_SPHERE_MAPPING_SHIFT 2u

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
	float4 sphereMapTexcoord : TEXCOORD1;
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
	float4x4 vertexProjectionTransform;
	float4x4 vertexEyeTransform;
	float3x3 normalTransform;
	float4x4 texcoordTransform;
	uint numLights;
	float materialShininess;
	float4 materialAmbient;
	float4 materialDiffuse;
	float4 materialSpecular;
	float4 materialEmission;
	float4 defaultVertexColor;
	LightData lights[MAX_LIGHTS];
};

// numLights == 0 means that lighting is disabled, otherwise lighting is enabled but the number of lights is numLights - 1
#define LIGHTING_ENABLED (numLights > 0u)
#define NUM_LIGHTS (numLights - 1u)

float4 transformTexcoord(const VertexData vertexData, const float2 texcoord)
{
	const float4 texcoord4 = float4(texcoord, 0.f, 1.f);
	const float4 transformedTexcoord = mul(texcoordTransform, texcoord4);
#if VERTEX_HAS_TEXTURE_MATRIX_SCALE
	return lerp(texcoord4, transformedTexcoord, float4(vertexData.textureMatrixScale, 1.f, 1.f));
#else
	return transformedTexcoord;
#endif
}

FragmentData VSMain(const VertexData vertexData)
{
	FragmentData fragmentData;
	fragmentData.position = mul(vertexProjectionTransform, float4(vertexData.position, 1.f));

	const float3 normal = normalize(mul(normalTransform, vertexData.normal));

	float4 finalLighting;
	if (LIGHTING_ENABLED)
	{
		float3 lightingAmbient = float3(0.f, 0.f, 0.f);
		float3 lightingDiffuse = float3(0.f, 0.f, 0.f);
		float3 lightingSpecular = float3(0.f, 0.f, 0.f);

		for (uint i = 0u; i < NUM_LIGHTS; ++i)
		{
			const float3 dir = -lights[i].direction;
			// Since we are in eye coordinaties, (0, 0, 1) is the view direction
			const float3 halfDir = normalize(dir + float3(0.f, 0.f, 1.f));

			lightingAmbient += lights[i].ambient.rgb;
			lightingDiffuse += lights[i].diffuse.rgb * saturate(dot(normal, dir));
			lightingSpecular += lights[i].specular.rgb * pow(saturate(dot(normal, halfDir)), materialShininess);
		}

		lightingAmbient *= materialAmbient.rgb;
		lightingDiffuse *= materialDiffuse.rgb;
		lightingSpecular *= materialSpecular.rgb;

		finalLighting = saturate(float4(lightingAmbient + lightingDiffuse + lightingSpecular + materialEmission.rgb, materialDiffuse.a));
	}
	else
		finalLighting = float4(1.f, 1.f, 1.f, 1.f);

#if VERTEX_HAS_COLOR
	const float4 vertexColor = (uint4(vertexData.color >> 16u, vertexData.color >> 8u, vertexData.color, vertexData.color >> 24u) & 0xffu) / 255.f;
#else
	static const float4 vertexColor = defaultVertexColor;
#endif

	fragmentData.color = vertexColor * finalLighting;
	fragmentData.texcoord = transformTexcoord(vertexData, vertexData.texcoord);

	const float3 vertexEyeDir = normalize(mul(vertexEyeTransform, float4(vertexData.position, 1.f)).xyz);
	const float3 vertexEyeReflection = vertexEyeDir - 2.f * dot(vertexEyeDir, normal) * normal;
	const float m = 2.f * length(vertexEyeReflection + float3(0.f, 0.f, 1.f));
	fragmentData.sphereMapTexcoord = transformTexcoord(vertexData, float2(vertexEyeReflection.x / m + 0.5f, vertexEyeReflection.y / m + 0.5f));

	return fragmentData;
}

cbuffer ConstantsPS : register(b0)
{
	uint numTextures;
	uint textureModes;
	bool alphaTestEnabled;
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

float4 PSMain(const FragmentData fragmentData) : SV_Target
{
	float4 color = fragmentData.color;
	const float2 texcoord = fragmentData.texcoord.xy / fragmentData.texcoord.w;
	const float2 sphereMapTexcoord = fragmentData.sphereMapTexcoord.xy / fragmentData.sphereMapTexcoord.w;

	[unroll(MAX_TEXTURES)]
	for (uint i = 0u; i < numTextures; ++i)
	{
		const uint textureMode = (textureModes >> (i * TEXTURE_SHIFT)) & TEXTURE_MODE_MASK;
		const bool sphereMapping = (textureModes >> (i * TEXTURE_SHIFT + TEXTURE_SPHERE_MAPPING_SHIFT)) & 1u;
		const float4 textureColor = textures[i].Sample(samplers[i], sphereMapping ? sphereMapTexcoord : texcoord);

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

	if (alphaTestEnabled && color.a <= 1.f / 256.f)
		discard;

	return color;
}
