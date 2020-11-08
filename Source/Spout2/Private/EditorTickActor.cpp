// Fill out your copyright notice in the Description page of Project Settings.


#include "EditorTickActor.h"

// Sets default values
AEditorTickActor::AEditorTickActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AEditorTickActor::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AEditorTickActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

bool AEditorTickActor::ShouldTickIfViewportsOnly() const
{
	return true;
}