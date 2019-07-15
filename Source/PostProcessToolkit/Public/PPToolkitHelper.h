#pragma once

#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "PPToolkitHelper.generated.h"

UCLASS()
class POSTPROCESSTOOLKIT_API APPToolkitHelper : public AActor
{
	GENERATED_BODY()

public:
	APPToolkitHelper();

	UPROPERTY(EditAnywhere)
		class UPPToolkitSceneColorCopyComponent* SceneColorCapture;
    
    UPROPERTY(EditAnywhere)
        class UPPToolkitProcessorComponent* ProcessorChain;
};



UCLASS()
class POSTPROCESSTOOLKIT_API UPPToolkitSceneColorCopyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPPToolkitSceneColorCopyComponent();

	UPROPERTY(EditAnywhere)
		class UTextureRenderTarget2D* RenderTarget = nullptr;

public:
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
};


USTRUCT(BlueprintType)
struct POSTPROCESSTOOLKIT_API FPPToolkitProcessor
{
    GENERATED_BODY()
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    class UTextureRenderTarget2D* SourceRenderTarget;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FName SourceName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    class UMaterialInterface* SourceMaterial;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    class UTextureRenderTarget2D* DestRenderTarget;
    
    UPROPERTY(Transient, VisibleDefaultsOnly)
    class UMaterialInstanceDynamic* SourceMaterialInst;
};

UCLASS()
class POSTPROCESSTOOLKIT_API UPPToolkitProcessorComponent : public UActorComponent
{
    GENERATED_BODY()
    
public:
    UPPToolkitProcessorComponent();
    
    UPROPERTY(EditAnywhere)
    TArray<FPPToolkitProcessor> ProcessorChain;
    
public:
    virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
    
protected:
    bool bProcessorChainDirty = true;
    void PrepareProcessorChain();
    void ExecuteProcessorChain();
};
