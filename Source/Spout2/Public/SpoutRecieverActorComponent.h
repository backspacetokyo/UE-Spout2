// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine.h"
#include "Components/ActorComponent.h"

#include "SpoutRecieverActorComponent.generated.h"

UCLASS( ClassGroup=(Custom), DisplayName = "Spout Reciever", meta=(BlueprintSpawnableComponent) )
class SPOUT2_API USpoutRecieverActorComponent : public UActorComponent
{
	GENERATED_BODY()

	struct SpoutRecieverContext;
	TSharedPtr<SpoutRecieverContext> context;

	UPROPERTY()
	UTexture2D* IntermediateTexture2D = nullptr;

	void Tick_RenderThread(FRHICommandListImmediate& RHICmdList, void* hSharehandle, FTextureRenderTargetResource* RenderTargetResource);

public:	
	
	USpoutRecieverActorComponent();

protected:
	
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout2")
	FName SubscribeName = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout2")
	UTextureRenderTarget2D* InputTexture = nullptr;
};
