#pragma once
#include "LightMapRendering.h"
#include "MeshMaterialShader.h"
#include "SceneTextureParameters.h"
#include "BasePassRendering.h"

#if RHI_RAYTRACING

// custom hit group class
class RENDERER_API FCustomMaterialCHS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FCustomMaterialCHS, MeshMaterial);

	FCustomMaterialCHS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer) {}
	FCustomMaterialCHS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// only compile for used with light diffraction materials
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform)
			&& Parameters.MaterialParameters.bIsUsedWithLightDiffraction
			&& Parameters.VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLocalVertexFactory"), FNAME_Find));
	}

	static void ModifyCompilationEnvironment(const FMeshMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const IConsoleVariable* CompileMaterialCHS = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.CompileMaterialCHS"));
		
		OutEnvironment.SetDefine(TEXT("USE_MATERIAL_CLOSEST_HIT_SHADER"), CompileMaterialCHS && CompileMaterialCHS->GetBool() ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1);
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

// should rendering custom ray tracing?
inline bool ShouldRenderRayTracingCustom()
{
	return IsRayTracingEnabled();
}

// prepare all custom ray tracing stuff here
void PrepareRayTracingCustoms(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);

// Render raytracing pass
void RenderRayTracingCustomDepth(FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View);

void RenderRayTracingLightDiffraction(FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View);

#endif // RHI_RAYTRACING