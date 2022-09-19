#pragma once
#include "GlobalShader.h"
#include "SceneRendering.h"
#include "ShaderCompilerCore.h"

#if RHI_RAYTRACING

// light diffraction ray generation shader class
class FLightDiffractionRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLightDiffractionRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLightDiffractionRGS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RAYGENSHADER"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampedSamplerState)
		SHADER_PARAMETER(float, DiffractionSampleCount)
		SHADER_PARAMETER(float, Repetition)
		SHADER_PARAMETER(float, LinesPerMM)
	END_SHADER_PARAMETER_STRUCT()
};

// output light diffraction
class FOutputLightDiffractionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOutputLightDiffractionCS)
	SHADER_USE_PARAMETER_STRUCT(FOutputLightDiffractionCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputSceneColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LightDiffraction)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HistoryDiffraction)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputHistoryDiffraction)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampedSamplerState)
		SHADER_PARAMETER_ARRAY(FVector4, JitterOffsets, [16])
		SHADER_PARAMETER(FMatrix, UnjitteredPrevWorldToClip)
	END_SHADER_PARAMETER_STRUCT()
};

#endif // RHI_RAYTRACING