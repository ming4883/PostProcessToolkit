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
#include "SceneViewExtension.h"
#include "Async.h"

#if ENGINE_MINOR_VERSION >= 22
#include "CommonRenderResources.h"
#endif


class FPPToolkitSceneViewExtension : public FSceneViewExtensionBase
{
public:
    TWeakObjectPtr<APPToolkitHelper> Helper;
    
    FPPToolkitSceneViewExtension( const FAutoRegister& AutoRegister,  APPToolkitHelper* InHelper)
        : FSceneViewExtensionBase( AutoRegister )
        , Helper(InHelper)
    {
    }
    
    /**
     * Called on game thread when creating the view family.
     */
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    
    /**
     * Called on game thread when creating the view.
     */
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
    
    /**
     * Called on game thread when view family is about to be rendered.
     */
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
    
    /**
     * Called on render thread at the start of rendering.
     */
    virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override {}
    
    /**
     * Called on render thread at the start of rendering, for each view, after PreRenderViewFamily_RenderThread call.
     */
    virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {}
    
    /**
     * Allows to render content after the 3D content scene, useful for debugging
     */
    virtual void PostRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override
    {
        auto* HelperPtr = Helper.Get();
        if (!HelperPtr)
            return;
        
        HelperPtr->SceneColorCapture->UpdateRenderTarget_RenderThread(RHICmdList, InView);
        
        auto HelperGT = Helper;
        
        AsyncTask(ENamedThreads::GameThread, [HelperGT]() {
            // code to execute on game thread here
            auto* HelperPtrGT = HelperGT.Get();
            if (!HelperPtrGT)
                return;
            
            HelperPtrGT->ProcessorChain->UpdateProcessorChain();
        });
    }
    
};

APPToolkitHelper::APPToolkitHelper()
{
	SceneColorCapture = CreateDefaultSubobject<UPPToolkitSceneColorCopyComponent>("SceneColorCapture");
	AddOwnedComponent(SceneColorCapture);
	
	ProcessorChain = CreateDefaultSubobject<UPPToolkitProcessorComponent>("ProcessorChain");
	AddOwnedComponent(ProcessorChain);
}

void APPToolkitHelper::BeginPlay()
{
    Super::BeginPlay();
    
    if (!SceneViewExtension)
        SceneViewExtension = FSceneViewExtensions::NewExtension<FPPToolkitSceneViewExtension>(this);
}

void APPToolkitHelper::EndPlay(const EEndPlayReason::Type Reason)
{
    SceneViewExtension.Reset();
    Super::EndPlay(Reason);
}

UPPToolkitSceneColorCopyComponent::UPPToolkitSceneColorCopyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPPToolkitSceneColorCopyComponent::UpdateRenderTarget_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
    if (!RenderTarget || !InView.bIsGameView)
		return;

	auto RTResource = RenderTarget->GetRenderTargetResource();
    
	uint32 GameVPSizeX = InView.UnscaledViewRect.Width(), GameVPSizeY = InView.UnscaledViewRect.Height();
	
	uint32 RTSizeX = (uint32)RenderTarget->SizeX, RTSizeY = (uint32)RenderTarget->SizeY;

    //UE_LOG(LogTemp, Log, TEXT("RTRes %p"), RTRes);
    static const FName RendererModuleName( "Renderer" );
    IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);
    
    SCOPED_DRAW_EVENT(RHICmdList, PPToolkitHelperCopySceneColor);
    //SCOPED_GPU_STAT(RHICmdList, PPToolkitHelperCopySceneColor);

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

        auto FeatureLevel = InView.Family->Scene->GetFeatureLevel();
        TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(FeatureLevel);
        TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
        TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

#if ENGINE_MINOR_VERSION >= 22
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
#else
        GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = RendererModule->GetFilterVertexDeclaration().VertexDeclarationRHI;
#endif
        GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
        GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
        GraphicsPSOInit.PrimitiveType = PT_TriangleList;

        SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
        
        FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
        auto SceneColorTextureRHI = SceneRenderTargets.GetSceneColorTexture();
        
        //UE_LOG(LogTemp, Log, TEXT("buf %d x %d; vp %d x %d"), SceneColorTextureRHI->GetSizeXYZ().X, SceneColorTextureRHI->GetSizeXYZ().Y, GameVPSizeX, GameVPSizeY);
        
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
}

UPPToolkitProcessorComponent::UPPToolkitProcessorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bProcessorChainDirty = true;
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
