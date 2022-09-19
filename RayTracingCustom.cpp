#include "RayTracingCustom.h"
#include "RayTracingCustomDepth.h"
#include "RayTracingLightDiffraction.h"
#include "ScreenPass.h"
#include "ScenePrivate.h"

#if RHI_RAYTRACING
IMPLEMENT_MATERIAL_SHADER_TYPE(,FCustomMaterialCHS, TEXT("/Engine/Private/RayTracing/RayTracingCustomHitShaders.usf"), TEXT("closesthit=CustomMaterialCHS"), SF_RayHitGroup);

void PrepareRayTracingCustoms(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (!IsRayTracingEnabled())
	{
		return;
	}

	// add custom depth shader
	TShaderMapRef<FCustomDepthRGS> CustomDepthRGS(View.ShaderMap);
	OutRayGenShaders.Add(CustomDepthRGS.GetRayTracingShader());

	// add light diffraction shader
	TShaderMapRef<FLightDiffractionRGS> LightDiffractionRGS(View.ShaderMap);
	OutRayGenShaders.Add(LightDiffractionRGS.GetRayTracingShader());
}

#endif // RHI_RAYTRACING