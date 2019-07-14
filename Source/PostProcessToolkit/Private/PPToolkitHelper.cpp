#include "PPToolkitHelper.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneRenderTargets.h"
#include "Modules/ModuleManager.h"
#include "PipelineStateCache.h"
//#include "CommonRenderResources.h"

APPToolkitHelper::APPToolkitHelper()
{
	SceneColorCapture = CreateDefaultSubobject<UPPToolkitSceneColorCopyComponent>("SceneColorCapture");
	AddOwnedComponent(SceneColorCapture);
    
    ProcessorChain = CreateDefaultSubobject<UPPToolkitProcessorComponent>("ProcessorChain");
    AddOwnedComponent(ProcessorChain);

}

UPPToolkitSceneColorCopyComponent::UPPToolkitSceneColorCopyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPPToolkitSceneColorCopyComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	//UE_LOG(LogTemp, Log, TEXT("Tick %f"), DeltaTime);
	if (!RenderTarget)
		return;

	auto RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	auto Scene = GetOwner()->GetWorld()->Scene;
	
	uint32 ViewportX = (uint32)RenderTarget->SizeX, ViewportY = (uint32)RenderTarget->SizeY;

	ENQUEUE_RENDER_COMMAND(PPToolkitHelperCopySceneColor)(
	[RTResource, Scene, ViewportX, ViewportY](FRHICommandListImmediate& RHICmdList)
	{
		//UE_LOG(LogTemp, Log, TEXT("RTRes %p"), RTRes);
        static const FName RendererModuleName( "Renderer" );
        IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);

		FRHIRenderPassInfo RPInfo(RTResource->GetRenderTargetTexture(), ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("PPToolkitHelperCopySceneColor"));
		{
			RHICmdList.SetViewport(0, 0, 0.0f, ViewportX, ViewportY, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			auto FeatureLevel = Scene->GetFeatureLevel();
			TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(FeatureLevel);
			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
			TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = RendererModule->GetFilterVertexDeclaration().VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
			auto SceneColorTextureRHI = SceneRenderTargets.GetSceneColorTexture();
			
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SceneColorTextureRHI);
            
			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,
				ViewportX, ViewportY,
				0.0f, 0.0f,
				1.0f, 1.0f,
				FIntPoint(ViewportX, ViewportY),
				FIntPoint(1, 1),
				*VertexShader,
				EDRF_Default);

		}
		RHICmdList.EndRenderPass();
		RHICmdList.CopyToResolveTarget(  
			RTResource->GetRenderTargetTexture(),  
			RTResource->TextureRHI,  
			FResolveParams());
	});
}

UPPToolkitProcessorComponent::UPPToolkitProcessorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UPPToolkitProcessorComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
}
