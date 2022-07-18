// Fill out your copyright notice in the Description page of Project Settings.

#include "SpoutRecieverActorComponent.h"

#include <string>

#include "Windows/AllowWindowsPlatformTypes.h" 
#include <d3d11on12.h>
#include "Spout.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "GlobalShader.h"
#include "UniformBuffer.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"
#include "MediaShaders.h"

static spoutSenderNames senders;

class FTextureCopyVertexShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTextureCopyVertexShader, Global);
public:

	static bool ShouldCache(EShaderPlatform Platform) { return true; }

	FTextureCopyVertexShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{}
	FTextureCopyVertexShader() {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FTextureCopyPixelShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTextureCopyPixelShader, Global);
public:

#if (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 25) || (ENGINE_MAJOR_VERSION == 5)
	LAYOUT_FIELD(FShaderResourceParameter, SrcTexture);
#else ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 24
	FShaderResourceParameter SrcTexture;

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParams = FGlobalShader::Serialize(Ar);
		Ar << SrcTexture;
		return bShaderHasOutdatedParams;
	}
#endif

	static bool ShouldCache(EShaderPlatform Platform) { return true; }

	FTextureCopyPixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		SrcTexture.Bind(Initializer.ParameterMap, TEXT("SrcTexture"));
	}
	FTextureCopyPixelShader() {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

struct FTextureVertex
{
	FVector4 Position;
	FVector2D UV;
};

IMPLEMENT_SHADER_TYPE(, FTextureCopyPixelShader, TEXT("/Plugin/Spout2/SpoutReceiverCopyShader.usf"), TEXT("MainPixelShader"), SF_Pixel)

//////////////////////////////////////////////////////////////////////////

struct USpoutRecieverActorComponent::SpoutRecieverContext
{
	unsigned int width = 0, height = 0;
	DXGI_FORMAT dwFormat = DXGI_FORMAT_UNKNOWN;
	EPixelFormat format = PF_Unknown;
	FRHITexture2D* Texture2D;

	ID3D11Device* D3D11Device = nullptr;
	ID3D11DeviceContext* Context = nullptr;

	ID3D12Device* D3D12Device = nullptr;
	ID3D11On12Device* D3D11on12Device = nullptr;
	ID3D11Resource* WrappedDX11Resource = nullptr;

	SpoutRecieverContext(unsigned int width, unsigned int height, DXGI_FORMAT dwFormat, FRHITexture2D* Texture2D)
		: width(width)
		, height(height)
		, dwFormat(dwFormat)
		, Texture2D(Texture2D)
	{
		if (dwFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
			format = PF_B8G8R8A8;
		else if (dwFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
			format = PF_FloatRGBA;
		else if (dwFormat == DXGI_FORMAT_R32G32B32A32_FLOAT)
			format = PF_A32B32G32R32F;

		FString RHIName = GDynamicRHI->GetName();

		if (RHIName == TEXT("D3D11"))
		{
			D3D11Device = (ID3D11Device*)GDynamicRHI->RHIGetNativeDevice();
			D3D11Device->GetImmediateContext(&Context);
		}
		else if (RHIName == TEXT("D3D12"))
		{
			D3D12Device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
			UINT DeviceFlags11 = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

			verify(D3D11On12CreateDevice(
				D3D12Device,
				DeviceFlags11,
				nullptr,
				0,
				nullptr,
				0,
				0,
				&D3D11Device,
				&Context,
				nullptr
			) == S_OK);

			verify(D3D11Device->QueryInterface(__uuidof(ID3D11On12Device), (void**)&D3D11on12Device) == S_OK);
			
			ID3D12Resource* NativeTex = (ID3D12Resource*)Texture2D->GetNativeResource();

			D3D11_RESOURCE_FLAGS rf11 = {};

			verify(D3D11on12Device->CreateWrappedResource(
				NativeTex, &rf11,
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PRESENT, __uuidof(ID3D11Resource),
				(void**)&WrappedDX11Resource) == S_OK);
		}
		else throw;
	}

	~SpoutRecieverContext()
	{
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

		if (D3D12Device)
		{
			D3D12Device->Release();
			D3D12Device = nullptr;
		}

		if (Context)
		{
			Context->Release();
			Context = nullptr;
		}

	}

	void CopyResource(ID3D11Resource* SrcTexture)
	{
		check(IsInRenderingThread());
		if (!GWorld || !SrcTexture) return;

		FString RHIName = GDynamicRHI->GetName();

		if (RHIName == TEXT("D3D11"))
		{
			ID3D11Texture2D* NativeTex = (ID3D11Texture2D*)Texture2D->GetNativeResource();

			Context->CopyResource(NativeTex, SrcTexture);
			Context->Flush();
		}
		else if (RHIName == TEXT("D3D12"))
		{
			D3D11on12Device->AcquireWrappedResources(&WrappedDX11Resource, 1);
			Context->CopyResource(WrappedDX11Resource, SrcTexture);
			D3D11on12Device->ReleaseWrappedResources(&WrappedDX11Resource, 1);
			Context->Flush();
		}
	}
};

//////////////////////////////////////////////////////////////////////////

USpoutRecieverActorComponent::USpoutRecieverActorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}

// Called when the game starts
void USpoutRecieverActorComponent::BeginPlay()
{
	Super::BeginPlay();
}

void USpoutRecieverActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void USpoutRecieverActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	check(IsInGameThread());
	
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OutputRenderTarget)
		return;

	unsigned int width = 0, height = 0;
	HANDLE hSharehandle = nullptr;
	DXGI_FORMAT dwFormat = DXGI_FORMAT_UNKNOWN;

	bool find_sender = senders.FindSender(TCHAR_TO_ANSI(*SubscribeName.ToString()), width, height, hSharehandle, (DWORD&)dwFormat);

	EPixelFormat format = PF_Unknown;

	if (dwFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
		format = PF_B8G8R8A8;
	else if (dwFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
		format = PF_FloatRGBA;
	else if (dwFormat == DXGI_FORMAT_R32G32B32A32_FLOAT)
		format = PF_A32B32G32R32F;

	if (!find_sender
		|| format == PF_Unknown)
		return;

	{
		if (!IntermediateTexture2D
			|| IntermediateTexture2D->GetSizeX() != width
			|| IntermediateTexture2D->GetSizeY() != height
			|| IntermediateTexture2D->GetPixelFormat() != format)
		{
			IntermediateTexture2D = UTexture2D::CreateTransient(width, height, format, FName("SpoutIntermediate"));
			IntermediateTexture2D->UpdateResource();

			context = nullptr;
		}

		ENQUEUE_RENDER_COMMAND(SpoutRecieverRenderThreadOp)([this, hSharehandle, width, height, dwFormat, format](FRHICommandListImmediate& RHICmdList) {
			check(IsInRenderingThread());

			if (!OutputRenderTarget || !IntermediateTexture2D)
				return;

			FTextureRenderTargetResource* OutputRenderTargetResource = OutputRenderTarget->GetRenderTargetResource();

			if (!context)
			{
				const FTextureResource* IntermediateTextureResource = IntermediateTexture2D->GetResource();
				check(IntermediateTextureResource);
				check(IntermediateTextureResource->TextureRHI);
			
				FTexture2DRHIRef IntermediateTextureRef = IntermediateTextureResource->TextureRHI->GetTexture2D();
				check(IntermediateTextureRef);
			
				context = TSharedPtr<SpoutRecieverContext>(new SpoutRecieverContext(width, height, dwFormat, IntermediateTextureRef));
			}

			this->Tick_RenderThread(RHICmdList, hSharehandle, OutputRenderTargetResource);
		});
	}
}

void USpoutRecieverActorComponent::Tick_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	void* hSharehandle,
	FTextureRenderTargetResource* OutputRenderTargetResource
)
{
	check(IsInRenderingThread());

	SCOPED_DRAW_EVENT(RHICmdList, ProcessSpoutCopyTexture);

	if (!GWorld || !GWorld->Scene || !context) return;

	ID3D11Resource* SrcTexture = nullptr;
	verify(context->D3D11Device->OpenSharedResource(hSharehandle, __uuidof(ID3D11Resource), (void**)(&SrcTexture)) == S_OK);
	check(SrcTexture);

	context->CopyResource(SrcTexture);
	SrcTexture->Release();

	FShaderResourceViewRHIRef IntermediateTextureParameterSRV;

	{
		auto Resource = IntermediateTexture2D->GetResource();
		auto ParamRef = (Resource->TextureRHI.GetReference());
		IntermediateTextureParameterSRV = RHICreateShaderResourceView(ParamRef, 0);
		check(IntermediateTextureParameterSRV.IsValid());
	}

	{
		auto ShaderResource = OutputRenderTargetResource->GetRenderTargetTexture(); 
		
		FRHIRenderPassInfo RPInfo(
			ShaderResource,
			ERenderTargetActions::DontLoad_Store);

		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopySpoutImage"));
		{
			FIntPoint OutputSize(
				OutputRenderTargetResource->GetSizeX(), OutputRenderTargetResource->GetSizeY());

			auto* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FMediaShadersVS> VertexShader(GlobalShaderMap);
			TShaderMapRef<FTextureCopyPixelShader> PixelShader(GlobalShaderMap);

			// Set the graphic pipeline state.
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
			
#if (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 25) || (ENGINE_MAJOR_VERSION == 5)
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
#else ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 24
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
#endif

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit,
				0, EApplyRendertargetOption::CheckApply, true);

			if (PixelShader->SrcTexture.IsBound())
			{
				auto PixelShaderRHI = GraphicsPSOInit.BoundShaderState.PixelShaderRHI;
				RHICmdList.SetShaderResourceViewParameter(PixelShaderRHI, PixelShader->SrcTexture.GetBaseIndex(), IntermediateTextureParameterSRV);
			}
			
			FBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer();
			RHICmdList.SetStreamSource(0, VertexBuffer, 0);
			RHICmdList.SetViewport(0, 0, 0.0, OutputSize.X, OutputSize.Y, 1.f);
			RHICmdList.DrawPrimitive(0, 2, 1);
		}
		RHICmdList.EndRenderPass();

		RHICmdList.CopyToResolveTarget(ShaderResource, ShaderResource, FResolveParams());
	}
}
