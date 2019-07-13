#pragma once

#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "PostProcessToolkitHelper.generated.h"

UCLASS()
class POSTPROCESSTOOLKIT_API APostProcessToolkitHelper : public AActor
{
	GENERATED_BODY()

public:
	APostProcessToolkitHelper();

	UPROPERTY(EditAnywhere)
		class UPostProcessToolkitHelperComponent* HelperComponent;
};

UCLASS()
class POSTPROCESSTOOLKIT_API UPostProcessToolkitHelperComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPostProcessToolkitHelperComponent();

	UPROPERTY(EditAnywhere)
		class UTextureRenderTarget2D* RenderTarget = nullptr;

	UPROPERTY(EditAnywhere)
		TArray<class UMaterialInterface*> PostProcessChain;

public:
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
};
