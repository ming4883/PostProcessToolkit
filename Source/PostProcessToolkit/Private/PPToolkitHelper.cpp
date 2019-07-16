#include "PPToolkitHelper.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneRenderTargets.h"
#include "Modules/ModuleManager.h"
#include "PipelineStateCache.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"
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
	// TODO: check is dedicated server
    
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

        auto RTTextureRHI = RTResource->GetRenderTargetTexture();
		FRHIRenderPassInfo RPInfo(RTTextureRHI, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("PPToolkitHelperCopySceneColor"));
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
            SetRenderTarget(RHICmdList, RTTextureRHI, FTextureRHIRef());
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
            RHICmdList.SetViewport(0, 0, 0.0f, ViewportX, ViewportY, 1.0f);
            
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
                0, 0,                                   // Dest X, Y
                ViewportX,                              // Dest Width
                ViewportY,                              // Dest Height
                0, 0,                                   // Source U, V
                1, 1,                                   // Source USize, VSize
                FIntPoint(ViewportX, ViewportY),        // Target buffer size
                FIntPoint(1, 1),                        // Source texture size
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
    bProcessorChainDirty = true;
}

void UPPToolkitProcessorComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
    // TODO: check is dedicated server
    PrepareProcessorChain();
    ExecuteProcessorChain();
}

void UPPToolkitProcessorComponent::PrepareProcessorChain()
{
    if (!bProcessorChainDirty)
        return;
    
    UWorld* World = GetOwner()->GetWorld();
    
    for(int32 i = 0; i < ProcessorChain.Num(); ++i)
    {
        auto& Processor = ProcessorChain[i];
        if (!Processor.SourceMaterial)
            continue;
        
        Processor.SourceMaterialInst = UKismetMaterialLibrary::CreateDynamicMaterialInstance(World, Processor.SourceMaterial);
    }
    
    bProcessorChainDirty = false;
}

void UPPToolkitProcessorComponent::ExecuteProcessorChain()
{
    UWorld* World = GetOwner()->GetWorld();
    
    FVector2D Zero(0, 0);
    FVector2D DrawSize;
    UCanvas* DrawCanvas;
    FDrawToRenderTargetContext DrawContext;
    
    for(int32 i = 0; i < ProcessorChain.Num(); ++i)
    {
        auto& Processor = ProcessorChain[i];
        if (!Processor.SourceMaterialInst)
            continue;
        
        if (!Processor.SourceRenderTarget)
            continue;
        
        if (!Processor.DestRenderTarget)
            continue;
        
        auto Mtl = Processor.SourceMaterialInst;
        Mtl->SetTextureParameterValue(Processor.SourceName, Processor.SourceRenderTarget);
        
        UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Processor.DestRenderTarget, DrawCanvas, DrawSize, DrawContext);
        DrawCanvas->K2_DrawMaterial(Mtl, Zero, DrawSize, Zero);
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, DrawContext);
    }
}
