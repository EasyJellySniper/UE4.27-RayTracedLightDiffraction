#include "RayTracingLightDiffraction.h"
#include "SceneTextureParameters.h"
#include "RayTracingCustom.h"
#include "Math/Halton.h"

#if RHI_RAYTRACING

#include "RayTracingMaterialHitShaders.h"
IMPLEMENT_GLOBAL_SHADER(FLightDiffractionRGS, "/Engine/Private/RayTracing/RayTracingLightDiffraction.usf", "LightDiffractionRG", SF_RayGen);
IMPLEMENT_GLOBAL_SHADER(FOutputLightDiffractionCS, "/Engine/Private/RayTracing/RayTracingLightDiffraction.usf", "OutputLightDiffractionCS", SF_Compute);

// toggable cvar
static TAutoConsoleVariable<bool> CVarRayTracingLightDiffraction(
	TEXT("r.RayTracing.LightDiffraction"),
	true,
	TEXT("Ray tracing diffraction ON/OFF.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingLightDiffractionSampleCount(
	TEXT("r.RayTracing.LightDiffraction.SampleCount"),
	4,
	TEXT("Ray tracing diffraction sample count.\n"),
	ECVF_RenderThreadSafe);

FTexture2DRHIRef HistoryDiffractionTexture;
TRefCountPtr<IPooledRenderTarget> HistoryDiffractionRT;

#endif // RHI_RAYTRACING

FVector TemporalRandom(uint32 FrameNumber)
{
	FVector RandomOffsetValue = FVector(Halton(FrameNumber & 1023, 2), Halton(FrameNumber & 1023, 3), Halton(FrameNumber & 1023, 5));
	return RandomOffsetValue;
}

void RenderRayTracingLightDiffraction(FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View)
#if RHI_RAYTRACING
{
	if (!IsRayTracingEnabled())
	{
		return;
	}
	
	if (!CVarRayTracingLightDiffraction->GetBool())
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "%s", TEXT("RenderRayTracingDiffraction"));
	
	// setup parameters
	FLightDiffractionRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLightDiffractionRGS::FParameters>();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
	PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	PassParameters->SceneDepthTexture = SceneTextures.SceneDepthTexture;
	PassParameters->GBufferA = SceneTextures.GBufferATexture;
	PassParameters->BilinearClampedSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->DiffractionSampleCount = FMath::Max(CVarRayTracingLightDiffractionSampleCount->GetInt(), 1);

	FIntPoint Resolution(View.ViewRect.Width(), View.ViewRect.Height());
	FIntPoint HalfResolution = FIntPoint::DivideAndRoundUp(Resolution, 2);

	// create output texture
	const FRDGTextureDesc TexDesc = FRDGTextureDesc::Create2D(HalfResolution, PF_R8G8B8A8, FClearValueBinding::Transparent, TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
	const FRDGTextureRef OutputColor = GraphBuilder.CreateTexture(TexDesc, TEXT("Output Light Diffraction"));
	PassParameters->OutputColor = GraphBuilder.CreateUAV(OutputColor);
	
	// add half-sized pass
	TShaderMapRef<FLightDiffractionRGS> RayGenerationShader(View.ShaderMap);
	ClearUnusedGraphResources(RayGenerationShader, PassParameters);

	// set r.RDG.CullPasses = 0, so we can preview the pass without output being used
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("RayTracedLightDiffraction %dx%d", HalfResolution.X, HalfResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute,
	[&View, RayGenerationShader, PassParameters, HalfResolution](FRHICommandList& RHICmdList)
	{
		FRayTracingShaderBindingsWriter GlobalResources;
		SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

		// the hit group and miss shader are bind in RayTracingMaterialPipeline, simply reuse it
		FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

		// if we want to reuse material pipeline, we need to add this pass after WaitForRayTracingScene() in DeferredShadingRenderer.cpp
		RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, HalfResolution.X, HalfResolution.Y);
	});

	// add output light diffraction pass
	FOutputLightDiffractionCS::FParameters* OutputParameters = GraphBuilder.AllocParameters<FOutputLightDiffractionCS::FParameters>();
	OutputParameters->View = View.ViewUniformBuffer;
	OutputParameters->LightDiffraction = OutputColor;
	OutputParameters->BilinearClampedSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();
	OutputParameters->SceneDepthTexture = SceneTextures.SceneDepthTexture;

	const FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	const FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), ERenderTargetTexture::ShaderResource);
	OutputParameters->OutputSceneColor = GraphBuilder.CreateUAV(SceneColorTexture);

	// setup jitter offset
	OutputParameters->JitterOffsets[0] = TemporalRandom(View.Family->FrameNumber);
	for (int32 FrameOffsetIndex = 1; FrameOffsetIndex < 16; FrameOffsetIndex++)
	{
		OutputParameters->JitterOffsets[FrameOffsetIndex] = TemporalRandom(View.Family->FrameNumber - FrameOffsetIndex);
	}

	// setup prev world to clip for history uv
	OutputParameters->UnjitteredPrevWorldToClip = View.PrevViewInfo.ViewMatrices.GetViewMatrix() * View.PrevViewInfo.ViewMatrices.ComputeProjectionNoAAMatrix();

	// prepare or update history buffer
	if (HistoryDiffractionTexture == nullptr || View.PrevViewInfo.ViewRect != View.ViewRect)
	{
		FRHIResourceCreateInfo Info;
		Info.DebugName = TEXT("History Diffraction Texture");
		Info.ClearValueBinding = FClearValueBinding::Transparent;

		HistoryDiffractionTexture.SafeRelease();
		HistoryDiffractionRT.SafeRelease();
		HistoryDiffractionTexture = RHICreateTexture2D(HalfResolution.X, HalfResolution.Y, PF_R8G8B8A8, 1, 1, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable, Info);
		HistoryDiffractionRT = CreateRenderTarget(HistoryDiffractionTexture, TEXT("History Diffraction RT"));

		const FRDGTextureRef TempRDG = GraphBuilder.RegisterExternalTexture(HistoryDiffractionRT);
		AddClearRenderTargetPass(GraphBuilder, TempRDG);
	}
	const FRDGTextureRef HistoryDiffractionRDG = GraphBuilder.RegisterExternalTexture(HistoryDiffractionRT);
	OutputParameters->HistoryDiffraction = GraphBuilder.CreateUAV(HistoryDiffractionRDG);
	OutputParameters->InputHistoryDiffraction = HistoryDiffractionRDG;

	TShaderMapRef<FOutputLightDiffractionCS> OutputLightDiffractionCS(View.ShaderMap);
	ClearUnusedGraphResources(OutputLightDiffractionCS, OutputParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Output Light Diffraction"),
		OutputParameters,
		ERDGPassFlags::Compute,
	[OutputLightDiffractionCS, OutputParameters, Resolution](FRHICommandList& RHICmdList)
	{
		const FIntPoint Groups = FIntPoint::DivideAndRoundUp(Resolution, 8);
		RHICmdList.SetComputeShader(OutputLightDiffractionCS.GetComputeShader());
		
		SetShaderParameters(RHICmdList, OutputLightDiffractionCS, OutputLightDiffractionCS.GetComputeShader(), *OutputParameters);
		DispatchComputeShader(RHICmdList, OutputLightDiffractionCS.GetShader(), Groups.X, Groups.Y, 1);
		UnsetShaderUAVs(RHICmdList, OutputLightDiffractionCS, OutputLightDiffractionCS.GetComputeShader());
	});
}
#else
{
	unimplemented();
}
#endif // RHI_RAYTRACING