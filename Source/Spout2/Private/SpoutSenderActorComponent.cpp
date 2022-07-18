// Fill out your copyright notice in the Description page of Project Settings.


#include "SpoutSenderActorComponent.h"

#include <string>
#include <map>

#include "Windows/AllowWindowsPlatformTypes.h" 
#include <d3d11on12.h>
#include "Spout.h"
#include "Windows/HideWindowsPlatformTypes.h"

static std::map<std::string, int> sender_name_reference_countor;

struct USpoutSenderActorComponent::SpoutSenderContext
{
	ID3D11Device* D3D11Device = nullptr;
	ID3D11On12Device* D3D11on12Device = nullptr;
	ID3D11Resource* WrappedDX11Resource = nullptr;

	spoutSenderNames senders;
	spoutDirectX sdx;

	FName Name;
	std::string Name_str;
	unsigned int width = 0, height = 0;

	HANDLE sharedSendingHandle = nullptr;
	ID3D11Texture2D* sendingTexture = nullptr;
	ID3D11DeviceContext* deviceContext = nullptr;

	FRHITexture2D* Texture2D = nullptr;

	SpoutSenderContext(const FName& Name,
		FRHITexture2D* Texture2D)
		: Name(Name)
		, Texture2D(Texture2D)
	{
		DXGI_FORMAT texFormat;
		FString RHIName = GDynamicRHI->GetName();

		if (RHIName == TEXT("D3D11"))
		{
			D3D11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
			D3D11Device->GetImmediateContext(&deviceContext);

			ID3D11Texture2D* NativeTex = (ID3D11Texture2D*)Texture2D->GetNativeResource();

			D3D11_TEXTURE2D_DESC desc;
			NativeTex->GetDesc(&desc);

			width = desc.Width;
			height = desc.Height;

			texFormat = desc.Format;
		}
		else if (RHIName == TEXT("D3D12"))
		{
			ID3D12Device* Device12 = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
			UINT DeviceFlags11 = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

			verify(D3D11On12CreateDevice(
				Device12,
				DeviceFlags11,
				nullptr,
				0,
				nullptr,
				0,
				0,
				&D3D11Device,
				&deviceContext,
				nullptr
			) == S_OK);

			verify(D3D11Device->QueryInterface(__uuidof(ID3D11On12Device), (void**)&D3D11on12Device) == S_OK);

			D3D12_RESOURCE_DESC desc;
			ID3D12Resource* NativeTex = (ID3D12Resource*)Texture2D->GetNativeResource();
			desc = NativeTex->GetDesc();

			width = desc.Width;
			height = desc.Height;

			texFormat = desc.Format;

			D3D11_RESOURCE_FLAGS rf11 = {};

			verify(D3D11on12Device->CreateWrappedResource(
				NativeTex, &rf11,
				D3D12_RESOURCE_STATE_COPY_SOURCE,
				D3D12_RESOURCE_STATE_PRESENT, __uuidof(ID3D11Resource),
				(void**)&WrappedDX11Resource) == S_OK);

		}
		else
		{
			throw;
		}

		if (texFormat == DXGI_FORMAT_B8G8R8A8_TYPELESS) {
			texFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		}

		Name_str = TCHAR_TO_ANSI(*Name.ToString());;

		verify(sdx.CreateSharedDX11Texture(D3D11Device, width, height, texFormat, &sendingTexture, sharedSendingHandle));

		if (sender_name_reference_countor.find(Name_str) == sender_name_reference_countor.end())
			sender_name_reference_countor[Name_str] = 0;

		sender_name_reference_countor[Name_str] += 1;

		verify(senders.CreateSender(Name_str.c_str(), width, height, sharedSendingHandle, texFormat));
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

		if (WrappedDX11Resource)
		{
			D3D11on12Device->ReleaseWrappedResources(&WrappedDX11Resource, 1);
			WrappedDX11Resource = nullptr;
		}

		if (D3D11on12Device)
		{
			D3D11on12Device->Release();
			D3D11on12Device = nullptr;
		}

		if (D3D11Device)
		{
			D3D11Device->Release();
			D3D11Device = nullptr;
		}
	}

	void Tick()
	{
		if (!deviceContext)
			return;

		FString RHIName = GDynamicRHI->GetName();

		if (RHIName == TEXT("D3D11"))
		{
			ENQUEUE_RENDER_COMMAND(SpoutSenderRenderThreadOp)([this](FRHICommandListImmediate& RHICmdList) {
				ID3D11Texture2D* NativeTex = (ID3D11Texture2D*)Texture2D->GetNativeResource();

				this->deviceContext->CopyResource(sendingTexture, NativeTex);
				this->deviceContext->Flush();

				verify(this->senders.UpdateSender(TCHAR_TO_ANSI(*this->Name.ToString()),
					this->width, this->height,
					this->sharedSendingHandle));
			});
		}
		else if (RHIName == TEXT("D3D12"))
		{
			ENQUEUE_RENDER_COMMAND(SpoutSenderRenderThreadOp)([this](FRHICommandListImmediate& RHICmdList) {
				this->D3D11on12Device->AcquireWrappedResources(&WrappedDX11Resource, 1);
				this->deviceContext->CopyResource(sendingTexture, WrappedDX11Resource);
				this->D3D11on12Device->ReleaseWrappedResources(&WrappedDX11Resource, 1);
				this->deviceContext->Flush();

				verify(this->senders.UpdateSender(TCHAR_TO_ANSI(*this->Name.ToString()),
					this->width, this->height,
					this->sharedSendingHandle));
			});
		}
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
		|| !OutputTexture->GetResource()->TextureRHI) return;

	auto Texture2D = OutputTexture->GetResource()->TextureRHI->GetTexture2D();
	if (!Texture2D)
	{
		context.Reset();
		return;
	}

	if (!Texture2D->GetNativeResource())
	{
		context.Reset();
		return;
	}

	if (!context.IsValid())
	{
		context = TSharedPtr<SpoutSenderContext>(new SpoutSenderContext(PublishName, Texture2D));
	}
	else if (PublishName != context->GetName())
	{
		context.Reset();
		return;
	}

	context->Tick();
}

