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


#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif
//#include "CommonRenderResources.h"

APPToolkitHelper::APPToolkitHelper()
{
	SceneColorCapture = CreateDefaultSubobject<UPPToolkitSceneColorCopyComponent>("SceneColorCapture");
	AddOwnedComponent(SceneColorCapture);
	
	ProcessorChain = CreateDefaultSubobject<UPPToolkitProcessorComponent>("ProcessorChain");
	AddOwnedComponent(ProcessorChain);
}

void APPToolkitHelper::Refresh()
{
	SceneColorCapture->UpdateRenderTarget();
	ProcessorChain->UpdateProcessorChain();
}

UPPToolkitSceneColorCopyComponent::UPPToolkitSceneColorCopyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPPToolkitSceneColorCopyComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// TODO: check is dedicated server
	UpdateRenderTarget();
}

void UPPToolkitSceneColorCopyComponent::UpdateRenderTarget()
{
	if (!RenderTarget)
		return;

	auto RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	auto Scene = GetOwner()->GetWorld()->Scene;
	auto GameViewportClient = GEngine->GameViewportForWorld(GetOwner()->GetWorld());
	uint32 GameVPSizeX = 1, GameVPSizeY = 1;
	/*
#if WITH_EDITOR
	if (GEditor->bIsSimulatingInEditor)
	{
		FViewport* pViewPort = GEditor->GetActiveViewport();
		GameVPSizeX = pViewPort->GetSizeXY().X;
		GameVPSizeY = pViewPort->GetSizeXY().Y;
		VpSource = TEXT("Editor");

	} else
#endif
	*/
	if (GameViewportClient)
	{
		FViewport* Vp = GameViewportClient->GetGameViewport();
		GameVPSizeX = Vp->GetSizeXY().X;
		GameVPSizeY = Vp->GetSizeXY().Y;
	}
	
	uint32 RTSizeX = (uint32)RenderTarget->SizeX, RTSizeY = (uint32)RenderTarget->SizeY;

	ENQUEUE_RENDER_COMMAND(PPToolkitHelperCopySceneColor)(
	[RTResource, Scene, RTSizeX, RTSizeY, GameVPSizeX, GameVPSizeY](FRHICommandListImmediate& RHICmdList)
	{
		//UE_LOG(LogTemp, Log, TEXT("RTRes %p"), RTRes);
		static const FName RendererModuleName( "Renderer" );
		IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);
        
        SCOPED_DRAW_EVENT(RHICmdList, PPToolkitHelperCopySceneColor);

		auto RTTextureRHI = RTResource->GetRenderTargetTexture();
		SetRenderTarget(RHICmdList, RTTextureRHI, FTextureRHIRef());
		FRHIRenderPassInfo RPInfo(RTTextureRHI, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("UPPToolkitSceneColorCopyComponent"));
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			RHICmdList.SetViewport(0, 0, 0.0f, RTSizeX, RTSizeY, 1.0f);
			
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
			
			UE_LOG(LogTemp, Log, TEXT("buf %d x %d; vp %d x %d"), SceneColorTextureRHI->GetSizeXYZ().X, SceneColorTextureRHI->GetSizeXYZ().Y, GameVPSizeX, GameVPSizeY);
			
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SceneColorTextureRHI);
			
			float UScale = FMath::Clamp(GameVPSizeX / (float)SceneColorTextureRHI->GetSizeXYZ().X, 0.0f, 1.0f);
			float VScale = FMath::Clamp(GameVPSizeY / (float)SceneColorTextureRHI->GetSizeXYZ().Y, 0.0f, 1.0f);
			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,                                   // Dest X, Y
				RTSizeX,                                // Dest Width
				RTSizeY,                                // Dest Height
				0, 0,                                   // Source U, V
				UScale, VScale,                         // Source USize, VSize
				FIntPoint(RTSizeX, RTSizeY),            // Target buffer size
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
	UpdateProcessorChain();
}

void UPPToolkitProcessorComponent::UpdateProcessorChain()
{
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
