// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine.h"
#include "Components/ActorComponent.h"
#include "SpoutSenderActorComponent.generated.h"

UCLASS( ClassGroup=(Custom), DisplayName="Spout Sender", meta=(BlueprintSpawnableComponent) )
class SPOUT2_API USpoutSenderActorComponent : public UActorComponent
{
	GENERATED_BODY()

	struct SpoutSenderContext;
	TSharedPtr<SpoutSenderContext> context;

public:	
	// Sets default values for this component's properties
	USpoutSenderActorComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout2")
	FName PublishName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout2")
	UTexture* OutputTexture;
};
