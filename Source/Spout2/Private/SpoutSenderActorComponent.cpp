// Fill out your copyright notice in the Description page of Project Settings.


#include "SpoutSenderActorComponent.h"

#include <string>
#include <map>

#include "Windows/AllowWindowsPlatformTypes.h" 
#include <d3d11.h>
#include "Spout.h"
#include "Windows/HideWindowsPlatformTypes.h"

static std::map<std::string, int> sender_name_reference_countor;

class USpoutSenderActorComponent::SpoutSenderContext
{
	ID3D11Device* D3D11Device = nullptr;
	spoutSenderNames senders;
	spoutDirectX sdx;

	FName Name;
	std::string Name_str;
	unsigned int width = 0, height = 0;

	HANDLE sharedSendingHandle = nullptr;
	ID3D11Texture2D* sendingTexture = nullptr;
	ID3D11Texture2D* sourceTexture = nullptr;
	ID3D11DeviceContext* deviceContext = nullptr;

public:

	SpoutSenderContext(const FName& Name,
		ID3D11Texture2D* sourceTexture)
		: Name(Name)
		, sourceTexture(sourceTexture)
	{
		D3D11Device = (ID3D11Device*)GDynamicRHI->RHIGetNativeDevice();

		D3D11_TEXTURE2D_DESC desc;
		sourceTexture->GetDesc(&desc);

		width = desc.Width;
		height = desc.Height;

		DXGI_FORMAT texFormat = desc.Format;
		if (desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS) {
			texFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		}

		ANSICHAR AnsiName[NAME_SIZE];
		Name.GetPlainANSIString(AnsiName);
		Name_str = AnsiName;

		verify(sdx.CreateSharedDX11Texture(D3D11Device, desc.Width, desc.Height, texFormat, &sendingTexture, sharedSendingHandle));

		if (sender_name_reference_countor.find(Name_str) == sender_name_reference_countor.end())
			sender_name_reference_countor[Name_str] = 0;

		sender_name_reference_countor[Name_str] += 1;

		verify(senders.CreateSender(AnsiName, desc.Width, desc.Height, sharedSendingHandle, texFormat));

		D3D11Device->GetImmediateContext(&deviceContext);
	}

	~SpoutSenderContext()
	{
		sender_name_reference_countor[Name_str] -= 1;

		if (sender_name_reference_countor[Name_str] == 0)
			senders.ReleaseSenderName(Name_str.c_str());

		if (sendingTexture)
		{
			sendingTexture->Release();
			sendingTexture = nullptr;
		}

		if (deviceContext)
		{
			deviceContext->Release();
			deviceContext = nullptr;
		}
	}

	void Tick()
	{
		if (!deviceContext)
			return;

		ENQUEUE_RENDER_COMMAND(SpoutSenderRenderThreadOp)([this](FRHICommandListImmediate& RHICmdList) {
			this->deviceContext->CopyResource(sendingTexture, sourceTexture);
			this->deviceContext->Flush();
		});

		D3D11_TEXTURE2D_DESC desc;
		sourceTexture->GetDesc(&desc);

		verify(senders.UpdateSender(Name_str.c_str(), desc.Width, desc.Height, sharedSendingHandle));
	}

	const FName& GetName() const { return Name; }

};

///////////////////////////////////////////////////////////////////////////////

USpoutSenderActorComponent::USpoutSenderActorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}

void USpoutSenderActorComponent::BeginPlay()
{
	Super::BeginPlay();

	 context.Reset();
}

void USpoutSenderActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	context.Reset();

	Super::EndPlay(EndPlayReason);
}

void USpoutSenderActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OutputTexture
		|| !OutputTexture->Resource->TextureRHI) return;

	auto Texture2D = OutputTexture->Resource->TextureRHI->GetTexture2D();
	if (!Texture2D)
	{
		context.Reset();
		return;
	}

	auto SourceTexture = (ID3D11Texture2D*)Texture2D->GetNativeResource();
	if (!SourceTexture)
	{
		context.Reset();
		return;
	}

	if (!context.IsValid())
	{
		context = TSharedPtr<SpoutSenderContext>(new SpoutSenderContext(PublishName, SourceTexture));
	}
	else if (PublishName != context->GetName())
	{
		context.Reset();
		return;
	}

	context->Tick();
}

