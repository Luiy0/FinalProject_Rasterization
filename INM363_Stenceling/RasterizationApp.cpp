//***************************************************************************************
// Framework by Frank Luna (C) 2015 All Rights Reserved.
// 
// Modified by Luis A. Flores on 29/10/2021
// luis-angel.flores-carrubio@city.ac.uk
// 
// Directional light movement with arrow keys
// For different effects hold keys 1 through 5
// 
// 
//***************************************************************************************

#include "d3dApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"
#include "Camera.h"
#include "ShadowMap.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Structure stores parameters to draw a shape
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

// Rendering layers for objects
enum class RenderLayer : int
{
	Opaque = 0,
	Debug,
	Mirrors,
	Reflected,
	Transparent,
	Shadow,
	AlphaTested,
	Sky,
	Count
};

// Class with function prototypes and member variables
class RasterizationApp : public D3DApp
{
public:
	RasterizationApp(HINSTANCE hInstance);
	RasterizationApp(const RasterizationApp& rhs) = delete;
	RasterizationApp& operator=(const RasterizationApp& rhs) = delete;
	~RasterizationApp();

	virtual bool Initialize()override;

private:
	virtual void CreateRtvAndDsvDescriptorHeaps()override;
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateReflectedPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildWavesGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawSceneToShadowMap();

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

	float GetHillsHeight(float x, float z)const;
	XMFLOAT3 GetHillsNormal(float x, float z)const;

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;

	int mCurrFrameResourceIndex = 0;
	UINT mCbvSrvDescriptorSize = 0;
	UINT mSkyTexHeapIndex = 0;
	UINT mShadowMapHeapIndex = 0;
	UINT mNullCubeSrvIndex = 0;
	UINT mNullTexSrvIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	RenderItem* mWavesRitem = nullptr;
	std::unique_ptr<Waves> mWaves;
	std::unique_ptr<ShadowMap> mShadowMap;

	DirectX::BoundingSphere mSceneBounds;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	PassConstants mMainPassCB; // index 0 of pass cbuffer
	PassConstants mReflectedPassCB; // index 1 of pass cbuffer
	PassConstants mShadowPassCB; // index 2 of pass cbuffer

	CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

	// Camera
	Camera mCam;

	// Control variables off by default
	bool mIsWireframe = false;
	bool mFog = false;
	bool mFlashLight = false;
	bool mStencilReflection = false;
	bool mStencilShadow = false;
	bool mShMap = false;
	bool mShMapDebug = false;

	// Directional Light (sun)
	float mSunTheta = 1.25f * XM_PI;
	float mSunPhi = XM_PIDIV4;

	float mLightNearZ = 0.0f;
	float mLightFarZ = 0.0f;
	XMFLOAT3 mLightPosW;
	XMFLOAT4X4 mLightView = MathHelper::Identity4x4();
	XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
	XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

	POINT mLastMousePos;
};



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		RasterizationApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

RasterizationApp::RasterizationApp(HINSTANCE hInstance): D3DApp(hInstance){

	// Estimate the scene bounding sphere manually since we know how the scene was constructed.
	// The grid is the "widest object" with a width of 20 and depth of 30.0f, and centered at
	// the world space origin.  In general, you need to loop over every world space vertex
	// position and compute the bounding sphere.
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(230 * 230 + 230 * 230); // GRID: width 460 + depth 460

}

RasterizationApp::~RasterizationApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool RasterizationApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Query the increment size of a descriptor for this heap type, this is hardware specific.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Initializing waves (m, n, dx, dt, speed, damping) 128
	mWaves = std::make_unique<Waves>(128, 128, 3.5f, 0.02f, 5.0f, 15.0f);

	// Camera initial position
	mCam.SetPosition(0.0f, 30.0f, 50.0f);
	mCam.LookAt(mCam.GetPosition3f(), { 0.0f, 0.0f, 0.0 }, mCam.GetUp3f());

	// Initializing shadow map resolution/quality
	mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), 4096, 4096);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildWavesGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void RasterizationApp::CreateRtvAndDsvDescriptorHeaps()
{
	// Add +6 RTV for cube render target.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	// Add +1 DSV for shadow map.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void RasterizationApp::LoadTextures()
{
	std::vector<std::string> texNames =
	{
		"grassTex",
		"waterTex",
		"fenceTex",
		"crateTex",
		"bricksTex",
		"stoneTex",
		"iceTex",
		"white1x1Tex",
		"defaultNormalMap",
		"skyCubeMap"
	};

	std::vector<std::wstring> texFilenames =
	{
		L"Textures/grass.dds",
		L"Textures/water1.dds",
		L"Textures/WireFence.dds",
		L"Textures/WoodCrate02.dds",
		L"Textures/bricks.dds",
		L"Textures/stone.dds",
		L"Textures/ice.dds",
		L"Textures/white1x1.dds",
		L"Textures/default_nmap.dds",
		L"Textures/grasscube1024.dds"
	};

	for (int i = 0; i < (int)texNames.size(); ++i)
	{
		auto texMap = std::make_unique<Texture>();
		texMap->Name = texNames[i];
		texMap->Filename = texFilenames[i];
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), 
			mCommandList.Get(), texMap->Filename.c_str(),
			texMap->Resource, texMap->UploadHeap));

		mTextures[texMap->Name] = std::move(texMap);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> RasterizationApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp,
		shadow
	};
}



void RasterizationApp::OnResize()
{
	D3DApp::OnResize();
	mCam.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void RasterizationApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void RasterizationApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void RasterizationApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCam.Pitch(dy);
		mCam.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void RasterizationApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	// Wireframe rendering
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;

	// Fog
	if (GetAsyncKeyState('2') & 0x8000)
		mFog = true;
	else
		mFog = false;

	// Spotlight
	if (GetAsyncKeyState('3') & 0x8000)
		mFlashLight = true;
	else
		mFlashLight = false;

	// Stencil reflection
	if (GetAsyncKeyState('4') & 0x8000)
		mStencilReflection = true;
	else
		mStencilReflection = false;

	// Stencil shadow
	if (GetAsyncKeyState('5') & 0x8000)
		mStencilShadow = true;
	else
		mStencilShadow = false;

	// Shadow map
	if (GetAsyncKeyState('6') & 0x8000)
		mShMap = true;
	else
		mShMap = false;

	// Directional light
	if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
		mSunTheta -= 1.0f * dt;
	}
	if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
		mSunTheta += 1.0f * dt;
	}
	if (GetAsyncKeyState(VK_UP) & 0x8000) {
		mSunPhi += 1.0f * dt;
	}
	if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
		mSunPhi -= 1.0f * dt;
	}
	mSunPhi = MathHelper::Clamp(mSunPhi, 0.1f, XM_PIDIV2);

	// Camera
	if (GetAsyncKeyState('W') & 0x8000)
		mCam.Walk(30.0f * dt);
	if (GetAsyncKeyState('S') & 0x8000)
		mCam.Walk(-30.0f * dt);
	if (GetAsyncKeyState('A') & 0x8000)
		mCam.Strafe(-30.0f * dt);
	if (GetAsyncKeyState('D') & 0x8000)
		mCam.Strafe(30.0f * dt);


	mCam.UpdateViewMatrix();
}



void RasterizationApp::BuildRootSignature()
{
	// Signature to define which resources are binded to the rendering pipeline

	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 13, 2, 0); // Table of shader resource views (SRVs)

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	// Perfomance TIP: Order from most frequent to least frequent.
	// A descriptor table defines a contiguous range of descriptors in the descriptor heap
	slotRootParameter[0].InitAsConstantBufferView(0); // register b0 (per object)
	slotRootParameter[1].InitAsConstantBufferView(1); // register b1 (per pass)
	slotRootParameter[2].InitAsShaderResourceView(0, 1); // materials buffer
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

	// Creating the root signature descriptor as an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
											  serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), 
													 serializedRootSig->GetBufferSize(),
													 IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void RasterizationApp::BuildDescriptorHeaps()
{

	// Create and populate the SRV heap with descriptors to texture resources
	// Create the SRV heap.
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 13;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	// Fill out the heap with descriptors.
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto fenceTex = mTextures["fenceTex"]->Resource;
	auto crateTex = mTextures["crateTex"]->Resource;
	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
	auto white1x1Tex = mTextures["white1x1Tex"]->Resource;
	auto defaultNormalMap = mTextures["defaultNormalMap"]->Resource;
	auto skyTex = mTextures["skyCubeMap"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = fenceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = crateTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(crateTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = iceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = white1x1Tex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = defaultNormalMap->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(defaultNormalMap.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyTex->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyTex.Get(), &srvDesc, hDescriptor);
	mSkyTexHeapIndex = 9; // starting from zero

	
	///////////////////////////////////
	mShadowMapHeapIndex = mSkyTexHeapIndex + 1;
	mNullCubeSrvIndex = mShadowMapHeapIndex + 1;
	mNullTexSrvIndex = mNullCubeSrvIndex + 1; // descriptor 12

	auto srvCpuStart = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto srvGpuStart = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	auto dsvCpuStart = mDsvHeap->GetCPUDescriptorHandleForHeapStart();


	auto nullSrv = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mNullCubeSrvIndex, mCbvSrvUavDescriptorSize);
	mNullSrv = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mNullCubeSrvIndex, mCbvSrvUavDescriptorSize);

	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

	mShadowMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mShadowMapHeapIndex, mCbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mShadowMapHeapIndex, mCbvSrvUavDescriptorSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, 1, mDsvDescriptorSize));


}

void RasterizationApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		//"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["shadowAlphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["debugVS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugPS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");


	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void RasterizationApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects.
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	// PSO for opaque wireframe objects.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));

	// PSO for transparent objects
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;
	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	// PSO for marking stencil mirrors.
	CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
	mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;

	D3D12_DEPTH_STENCIL_DESC mirrorDSS;
	mirrorDSS.DepthEnable = true;
	mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	mirrorDSS.StencilEnable = true;
	mirrorDSS.StencilReadMask = 0xff;
	mirrorDSS.StencilWriteMask = 0xff;
	mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPsoDesc = opaquePsoDesc;
	markMirrorsPsoDesc.BlendState = mirrorBlendState;
	markMirrorsPsoDesc.DepthStencilState = mirrorDSS;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&markMirrorsPsoDesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])));

	// PSO for stencil reflections.
	D3D12_DEPTH_STENCIL_DESC reflectionsDSS;
	reflectionsDSS.DepthEnable = true;
	reflectionsDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionsDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionsDSS.StencilEnable = true;
	reflectionsDSS.StencilReadMask = 0xff;
	reflectionsDSS.StencilWriteMask = 0xff;
	reflectionsDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	reflectionsDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPsoDesc = opaquePsoDesc;
	drawReflectionsPsoDesc.DepthStencilState = reflectionsDSS;
	drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawReflectionsPsoDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));

	// PSO for shadow objects with transparency
	D3D12_DEPTH_STENCIL_DESC shadowDSS;
	shadowDSS.DepthEnable = true;
	shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDSS.StencilEnable = true;
	shadowDSS.StencilReadMask = 0xff;
	shadowDSS.StencilWriteMask = 0xff;
	shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	shadowDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPsoDesc;
	shadowPsoDesc.DepthStencilState = shadowDSS;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow"])));


	// PSO for alpha tested objects
	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));


	// PSO for shadow map pass.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = opaquePsoDesc;
	smapPsoDesc.RasterizerState.DepthBias = 100000;
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	smapPsoDesc.pRootSignature = mRootSignature.Get();
	smapPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
		mShaders["shadowVS"]->GetBufferSize()
	};
	smapPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()),
		mShaders["shadowOpaquePS"]->GetBufferSize()
	};

	// Shadow map pass does not have a render target.
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	smapPsoDesc.NumRenderTargets = 0;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

	// PSO for debug layer.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = opaquePsoDesc;
	debugPsoDesc.pRootSignature = mRootSignature.Get();
	debugPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()),
		mShaders["debugVS"]->GetBufferSize()
	};
	debugPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()),
		mShaders["debugPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));

	// PSO for sky.
	// The normalized depth values at z = 1 (NDC) will fail the depth test if the depth buffer was cleared to 1.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // The camera is inside the sky sphere, turn off culling.
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // Use less than or equal
	skyPsoDesc.pRootSignature = mRootSignature.Get();
	skyPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	

}

void RasterizationApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		// ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT waveVertCount
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			3, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
			// 0 main pass, 1 mirror pass, 2 shadow 
	}
}



void RasterizationApp::BuildShapeGeometry()
{

	// We are concatenating all the geometry into one big vertex/index buffer.
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(3.0f, 2.0f, 3.0f, 5); // width, height, depth, subdivisions
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(460.0f, 460.0f, 60, 40); // width, depth, m, n
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20); // radius, sliceCount, stackCount
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f); // x-position, y-position, width, height, z-position
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(3.0f, 2.5f, 3.0f, 5);

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT pyramidVertexOffset = quadVertexOffset + (UINT)quad.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT pyramidIndexOffset = quadIndexOffset + (UINT)quad.Indices32.size();

	// Define the SubmeshGeometry that cover different regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	// Extract the vertex elements we are interested in and pack the vertices of all the meshes into one vertex buffer.

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		quad.Vertices.size() +
		pyramid.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);
	UINT k = 0;

	// Box
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	// Grid
	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[k].Pos = { p.x, GetHillsHeight(p.x, p.z), p.z };
		vertices[k].Normal = GetHillsNormal(p.x, p.z);
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	// Sphere
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}

	// Cylinder
	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}

	// Quad
	for (size_t i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
		if (i == 0)
		{
			vertices[k].Pos = quad.Vertices[i].Position;
			vertices[k].Normal = quad.Vertices[i].Normal;
			vertices[k].TexC = quad.Vertices[i].TexC;
			vertices[k].TangentU = quad.Vertices[i].TangentU;
		}
		else
		{
			vertices[k].Pos = quad.Vertices[i].Position;
			vertices[k].Normal = quad.Vertices[i].Normal;
			vertices[k].TexC = quad.Vertices[i].TexC;
			vertices[k].TangentU = quad.Vertices[i].TangentU;
		}
	}

	// Pyramid
	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
		vertices[k].TexC = pyramid.Vertices[i].TexC;
		vertices[k].TangentU = pyramid.Vertices[i].TangentU;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["quad"] = quadSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void RasterizationApp::BuildWavesGeometry()
{
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

	// Iterate over each quad.
	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void RasterizationApp::BuildMaterials()
{
	auto grass = std::make_unique<Material>();
	grass->Name = "grassMat";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->NormalSrvHeapIndex = 8;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.9f;  // increased to reduce environment map reflection

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "waterMat";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->NormalSrvHeapIndex = 8;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefenceMat";
	wirefence->MatCBIndex = 2;
	wirefence->DiffuseSrvHeapIndex = 2;
	wirefence->NormalSrvHeapIndex = 8;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;

	auto woodCrate = std::make_unique<Material>();
	woodCrate->Name = "woodCrateMat";
	woodCrate->MatCBIndex = 3;
	woodCrate->DiffuseSrvHeapIndex = 3;
	woodCrate->NormalSrvHeapIndex = 8;
	woodCrate->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	woodCrate->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	woodCrate->Roughness = 0.2f;

	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricksMat";
	bricks0->MatCBIndex = 4;
	bricks0->DiffuseSrvHeapIndex = 4;
	bricks0->NormalSrvHeapIndex = 8;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	bricks0->Roughness = 0.9f; // increased to reduce environment map reflection

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stoneMat";
	stone0->MatCBIndex = 5;
	stone0->DiffuseSrvHeapIndex = 5;
	stone0->NormalSrvHeapIndex = 8;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto icemirror = std::make_unique<Material>();
	icemirror->Name = "icemirrorMat";
	icemirror->MatCBIndex = 6;
	icemirror->DiffuseSrvHeapIndex = 6;
	icemirror->NormalSrvHeapIndex = 8;
	icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	icemirror->Roughness = 0.5f;

	auto shadow = std::make_unique<Material>();
	shadow->Name = "shadowMat";
	shadow->MatCBIndex = 7;
	shadow->DiffuseSrvHeapIndex = 7;
	shadow->NormalSrvHeapIndex = 8;
	shadow->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
	shadow->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
	shadow->Roughness = 0.9f;

	// Mirror materials will reflect the skybox texture (shader implemented)
	auto mirror = std::make_unique<Material>();
	mirror->Name = "mirrorMat";
	mirror->MatCBIndex = 8;
	mirror->DiffuseSrvHeapIndex = 7;
	mirror->NormalSrvHeapIndex = 8;
	mirror->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	mirror->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
	mirror->Roughness = 0.3f;

	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = 9;
	sky->DiffuseSrvHeapIndex = 8;
	sky->NormalSrvHeapIndex = 8;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;


	mMaterials["grassMat"] = std::move(grass);
	mMaterials["waterMat"] = std::move(water);
	mMaterials["wirefenceMat"] = std::move(wirefence);
	mMaterials["woodCrateMat"] = std::move(woodCrate);
	mMaterials["bricksMat"] = std::move(bricks0);
	mMaterials["stoneMat"] = std::move(stone0);
	mMaterials["icemirrorMat"] = std::move(icemirror);
	mMaterials["shadowMat"] = std::move(shadow);
	mMaterials["mirrorMat"] = std::move(mirror);
	mMaterials["sky"] = std::move(sky);

}

void RasterizationApp::BuildRenderItems()
{

	auto wavesRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(10.0f, 10.0f, 10.0f));
	XMStoreFloat4x4(&wavesRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, -1.0f, 0.0f));
	wavesRitem->ObjCBIndex = 0;
	wavesRitem->Mat = mMaterials["waterMat"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mWavesRitem = wavesRitem.get();
	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());
	mAllRitems.push_back(std::move(wavesRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(10.0f, 10.0f, 10.0f));
	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = mMaterials["grassMat"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 4.3f, 2.0f));
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["stoneMat"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

	auto pyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 8.3f, 2.0f));
	pyramidRitem->ObjCBIndex = 3;
	pyramidRitem->Mat = mMaterials["bricksMat"].get();
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidRitem.get());
	mAllRitems.push_back(std::move(pyramidRitem));

	auto leftCylRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftCylRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 5.0f, 34.0f));
	leftCylRitem->ObjCBIndex = 4;
	leftCylRitem->Mat = mMaterials["stoneMat"].get();
	leftCylRitem->Geo = mGeometries["shapeGeo"].get();
	leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
	mAllRitems.push_back(std::move(leftCylRitem));

	auto rightCylRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightCylRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 5.0f, 34.0f));
	rightCylRitem->ObjCBIndex = 5;
	rightCylRitem->Mat = mMaterials["stoneMat"].get();
	rightCylRitem->Geo = mGeometries["shapeGeo"].get();
	rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
	mAllRitems.push_back(std::move(rightCylRitem));

	auto leftSphereRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftSphereRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 13.0f, 34.0f));
	leftSphereRitem->ObjCBIndex = 6;
	leftSphereRitem->Mat = mMaterials["mirrorMat"].get();
	leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
	leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
	mAllRitems.push_back(std::move(leftSphereRitem));

	auto rightSphereRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightSphereRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 13.0f, 34.0f));
	rightSphereRitem->ObjCBIndex = 7;
	rightSphereRitem->Mat = mMaterials["mirrorMat"].get();
	rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
	rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());
	mAllRitems.push_back(std::move(rightSphereRitem));

	auto iceMirrorRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&iceMirrorRitem->TexTransform, XMMatrixScaling(10.0f, 10.0f, 10.0f));
	XMStoreFloat4x4(&iceMirrorRitem->World, XMMatrixRotationX(XM_PI) * XMMatrixScaling(50.0f, 50.0f, 50.0f) * XMMatrixTranslation(-20.0f, 0.0f, -20.0f));
	iceMirrorRitem->TexTransform = MathHelper::Identity4x4();
	iceMirrorRitem->ObjCBIndex = 8;
	iceMirrorRitem->Mat = mMaterials["icemirrorMat"].get();
	iceMirrorRitem->Geo = mGeometries["shapeGeo"].get();
	iceMirrorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	iceMirrorRitem->IndexCount = iceMirrorRitem->Geo->DrawArgs["quad"].IndexCount;
	iceMirrorRitem->StartIndexLocation = iceMirrorRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	iceMirrorRitem->BaseVertexLocation = iceMirrorRitem->Geo->DrawArgs["quad"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Mirrors].push_back(iceMirrorRitem.get());
	mRitemLayer[(int)RenderLayer::Transparent].push_back(iceMirrorRitem.get());
	mAllRitems.push_back(std::move(iceMirrorRitem));

	// Reflected box will have different world matrix, so it needs to be its own render item.
	auto reflectedBoxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&reflectedBoxRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 4.0f, 2.0f));
	reflectedBoxRitem->ObjCBIndex = 9;
	reflectedBoxRitem->Mat = mMaterials["stoneMat"].get();
	reflectedBoxRitem->Geo = mGeometries["shapeGeo"].get();
	reflectedBoxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	reflectedBoxRitem->IndexCount = reflectedBoxRitem->Geo->DrawArgs["box"].IndexCount;
	reflectedBoxRitem->StartIndexLocation = reflectedBoxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	reflectedBoxRitem->BaseVertexLocation = reflectedBoxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedBoxRitem.get());
	mAllRitems.push_back(std::move(reflectedBoxRitem));

	// Shadowed box will have different world matrix, so it needs to be its own render item.
	auto shadowedBoxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&shadowedBoxRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 4.0f, 2.0f));
	shadowedBoxRitem->ObjCBIndex = 10;
	shadowedBoxRitem->Mat = mMaterials["shadowMat"].get();
	shadowedBoxRitem->Geo = mGeometries["shapeGeo"].get();
	shadowedBoxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	shadowedBoxRitem->IndexCount = shadowedBoxRitem->Geo->DrawArgs["box"].IndexCount;
	shadowedBoxRitem->StartIndexLocation = shadowedBoxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	shadowedBoxRitem->BaseVertexLocation = shadowedBoxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedBoxRitem.get());
	mAllRitems.push_back(std::move(shadowedBoxRitem));

	auto reflectedPyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&reflectedPyramidRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 8.0f, 2.0f));
	reflectedPyramidRitem->ObjCBIndex = 11;
	reflectedPyramidRitem->Mat = mMaterials["bricksMat"].get();
	reflectedPyramidRitem->Geo = mGeometries["shapeGeo"].get();
	reflectedPyramidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	reflectedPyramidRitem->IndexCount = reflectedPyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	reflectedPyramidRitem->StartIndexLocation = reflectedPyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	reflectedPyramidRitem->BaseVertexLocation = reflectedPyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedPyramidRitem.get());
	mAllRitems.push_back(std::move(reflectedPyramidRitem));

	auto shadowedPyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&shadowedPyramidRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 8.0f, 2.0f));
	shadowedPyramidRitem->ObjCBIndex = 12;
	shadowedPyramidRitem->Mat = mMaterials["shadowMat"].get();
	shadowedPyramidRitem->Geo = mGeometries["shapeGeo"].get();
	shadowedPyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	shadowedPyramidRitem->IndexCount = shadowedPyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	shadowedPyramidRitem->StartIndexLocation = shadowedPyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	shadowedPyramidRitem->BaseVertexLocation = shadowedPyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedPyramidRitem.get());
	mAllRitems.push_back(std::move(shadowedPyramidRitem));

	auto shadowedLeftCylRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&shadowedLeftCylRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 5.0f, 34.0f));
	shadowedLeftCylRitem->ObjCBIndex = 13;
	shadowedLeftCylRitem->Mat = mMaterials["shadowMat"].get();
	shadowedLeftCylRitem->Geo = mGeometries["shapeGeo"].get();
	shadowedLeftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	shadowedLeftCylRitem->IndexCount = shadowedLeftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	shadowedLeftCylRitem->StartIndexLocation = shadowedLeftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	shadowedLeftCylRitem->BaseVertexLocation = shadowedLeftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedLeftCylRitem.get());
	mAllRitems.push_back(std::move(shadowedLeftCylRitem));

	auto shadowedRightCylRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&shadowedRightCylRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 5.0f, 34.0f));
	shadowedRightCylRitem->ObjCBIndex = 14;
	shadowedRightCylRitem->Mat = mMaterials["shadowMat"].get();
	shadowedRightCylRitem->Geo = mGeometries["shapeGeo"].get();
	shadowedRightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	shadowedRightCylRitem->IndexCount = shadowedRightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	shadowedRightCylRitem->StartIndexLocation = shadowedRightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	shadowedRightCylRitem->BaseVertexLocation = shadowedRightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedRightCylRitem.get());
	mAllRitems.push_back(std::move(shadowedRightCylRitem));

	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = 15;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));

	auto shadowGridRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&shadowGridRitem->TexTransform, XMMatrixScaling(10.0f, 10.0f, 10.0f));
	XMStoreFloat4x4(&shadowGridRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	shadowGridRitem->ObjCBIndex = 16;
	shadowGridRitem->Mat = mMaterials["shadowMat"].get();
	shadowGridRitem->Geo = mGeometries["shapeGeo"].get();
	shadowGridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	shadowGridRitem->IndexCount = shadowGridRitem->Geo->DrawArgs["grid"].IndexCount;
	shadowGridRitem->StartIndexLocation = shadowGridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	shadowGridRitem->BaseVertexLocation = shadowGridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowGridRitem.get());
	mAllRitems.push_back(std::move(shadowGridRitem));

	/*
	auto quadDebugRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&quadDebugRitem->World, XMMatrixScaling(10.0f, 1.0f, 1.0f) * XMMatrixTranslation(1.0f, 1.0f, 1.0f));
	quadDebugRitem->TexTransform = MathHelper::Identity4x4();
	quadDebugRitem->ObjCBIndex = 16;
	quadDebugRitem->Mat = mMaterials["bricksMat"].get();
	quadDebugRitem->Geo = mGeometries["shapeGeo"].get();
	quadDebugRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	quadDebugRitem->IndexCount = quadDebugRitem->Geo->DrawArgs["quad"].IndexCount;
	quadDebugRitem->StartIndexLocation = quadDebugRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	quadDebugRitem->BaseVertexLocation = quadDebugRitem->Geo->DrawArgs["quad"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Debug].push_back(quadDebugRitem.get());
	mAllRitems.push_back(std::move(quadDebugRitem));
	*/


	/*
	XMMATRIX brickTexTransform = XMMatrixScaling(4.0f, 4.0f, 4.0f);
	UINT objCBIndex = 20;
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(25.0f, 5.5f, 30.0f + i * 10.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(0.0f, 5.5f, 30.0f + i * 10.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(25.0f, 15.5f, 30.0f + i * 10.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(0.0f, 15.5f, 30.0f + i * 10.0f);

		XMStoreFloat4x4(&leftCylRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricksMat"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricksMat"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, XMMatrixScaling(6.0f, 6.0f, 6.0f) * leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["icemirrorMat"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, XMMatrixScaling(6.0f, 6.0f, 6.0f) * rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["icemirrorMat"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;


		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		mRitemLayer[(int)RenderLayer::Mirrors].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Transparent].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Mirrors].push_back(rightSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Transparent].push_back(rightSphereRitem.get());

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));

	}

	*/

}

float RasterizationApp::GetHillsHeight(float x, float z)const
{
	return 0.05f * (z * sinf(0.1f * x ) + x * cosf(0.1f * z));
}

XMFLOAT3 RasterizationApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.005f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.05f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}



void RasterizationApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	if (mIsWireframe) // true when holding key 1
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}


	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	// Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and set as a root descriptor.
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Bind null SRV for shadow map pass.
	mCommandList->SetGraphicsRootDescriptorTable(3, mNullSrv);
	
	// Bind all the textures used in this scene. The root signature knows how many descriptors are expected in the table.
	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());
		
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	// Bind the sky cube map
	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);
	
	// Opaque Objects
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	if (!mIsWireframe)
		mCommandList->SetPipelineState(mPSOs["debug"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Debug]);

	// Sky box
	if (!mIsWireframe)
		mCommandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

	// Mirrors Objects: Mark the visible mirror pixels in the stencil buffer with the value 1
	// Second pass
	if (mStencilReflection) {
		mCommandList->OMSetStencilRef(1);
		if (!mIsWireframe)
			mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Mirrors]);

		// Draw the reflection into the mirror only (only for pixels where the stencil buffer is 1).
		// Note that we must supply a different per-pass constant buffer--one with the lights reflected.
		UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
		mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
		if (!mIsWireframe)
			mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Reflected]);

		// Restore main pass constants and stencil ref.
		mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());
		mCommandList->OMSetStencilRef(0);
	}

	// Transparent Objects
	if (!mIsWireframe)
		mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

	// Alpha blend test (fences)
	if (!mIsWireframe)
		mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

	// Shadows objects
	if (mStencilShadow) {
		if (!mIsWireframe)
			mCommandList->SetPipelineState(mPSOs["shadow"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Shadow]);

	}

	// Shadow map
	// Third pass
	DrawSceneToShadowMap();

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void RasterizationApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void RasterizationApp::DrawSceneToShadowMap()
{
	
	mCommandList->RSSetViewports(1, &mShadowMap->Viewport());
	mCommandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());

	// Change to DEPTH_WRITE.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearDepthStencilView(mShadowMap->Dsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Set null render target because we are only going to draw to
	// depth buffer.  Setting a null render target will disable color writes.
	// Note the active PSO also must specify a render target count of 0.
	mCommandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

	// Bind the pass constant buffer for the shadow map pass.
	// Same root parameter as stencil but a different CB for third pass)
	auto passCB = mCurrFrameResource->PassCB->Resource();
	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 2 * passCBByteSize; // third pass
	mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);
	
	if (!mIsWireframe)
		mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
	
	// Change back to GENERIC_READ so we can read the texture in a shader.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}



void RasterizationApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	
	// Stencil reflections: updating reflection world matrix.
	if (mStencilReflection) {
		XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
		XMMATRIX R = XMMatrixReflect(mirrorPlane);

		XMStoreFloat4x4(&mAllRitems.at(9)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(3.0f, 8.0f, -5.0f) * R);
		XMStoreFloat4x4(&mAllRitems.at(10)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 4.0f, 2.0f) * R);
		XMStoreFloat4x4(&mAllRitems.at(11)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(3.0f, 12.0f, -5.0f) * R);
		XMStoreFloat4x4(&mAllRitems.at(12)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 8.0f, 2.0f) * R);
	}

	// Stencil shadows: updating shadow world matrix.
	if (mStencilShadow) {
		XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
		XMVECTOR directionalLight = -XMLoadFloat3(&mMainPassCB.Lights[0].Direction);
		XMVECTOR spotLight = -XMLoadFloat3(&mMainPassCB.Lights[6].Direction);
		XMMATRIX D = XMMatrixShadow(shadowPlane, directionalLight);
		XMMATRIX S = XMMatrixShadow(shadowPlane, spotLight);

		XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 1.1f, 0.0f);
		XMMATRIX shadowOffsetY2 = XMMatrixTranslation(0.0f, 4.0f, 0.0f);
		XMMATRIX shadowOffsetY3 = XMMatrixTranslation(0.0f, 0.00001f, 0.0f);

		if (mFlashLight)
		{
			XMStoreFloat4x4(&mAllRitems.at(10)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 4.0f, 2.0f) * S * shadowOffsetY); // box
			XMStoreFloat4x4(&mAllRitems.at(12)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 8.0f, 2.0f) * S * shadowOffsetY); // pyramid
			XMStoreFloat4x4(&mAllRitems.at(13)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 5.0f, 34.0f) * S * shadowOffsetY2); // cylinder
			XMStoreFloat4x4(&mAllRitems.at(14)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 5.0f, 34.0f) * S * shadowOffsetY2); // cylinder
			XMStoreFloat4x4(&mAllRitems.at(16)->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 0.01f, 0.0f) * S * shadowOffsetY3); // grid

		}
		else
		{
			XMStoreFloat4x4(&mAllRitems.at(10)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 4.0f, 2.0f) * D * shadowOffsetY);
			XMStoreFloat4x4(&mAllRitems.at(12)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(6.0f, 8.0f, 2.0f) * D * shadowOffsetY);
			XMStoreFloat4x4(&mAllRitems.at(13)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 5.0f, 34.0f) * D * shadowOffsetY2);
			XMStoreFloat4x4(&mAllRitems.at(14)->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-47.0f, 5.0f, 34.0f) * D * shadowOffsetY2);
			XMStoreFloat4x4(&mAllRitems.at(16)->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 0.01f, 0.0f) * D * shadowOffsetY3);
		}

	}

	if (mStencilReflection || mStencilShadow) {
		for (int i = 0; i < 6; ++i)
			mAllRitems.at(i + 9)->NumFramesDirty = gNumFrameResources;
		mAllRitems.at(16)->NumFramesDirty = gNumFrameResources;
	}


	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
	UpdateReflectedPassCB(gt);
	UpdateShadowPassCB(gt);	
	UpdateWaves(gt);
	
}

void RasterizationApp::UpdateWaves(const GameTimer& gt)
{
	// Generate a random wave.
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);

		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void RasterizationApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["waterMat"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.02f * gt.DeltaTime();
	tv += 0.005f * gt.DeltaTime();

	if (tu >= 1.0f)
		tu -= 1.0f;

	if (tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void RasterizationApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void RasterizationApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.NormalMapIndex = mat->NormalSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void RasterizationApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCam.GetView();
	XMMATRIX proj = mCam.GetProj();
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);
	XMMATRIX shadowTransform = XMLoadFloat4x4(&mShadowTransform);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));
	mMainPassCB.EyePosW = mCam.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.20f, 0.20f, 0.20f, 1.0f };

	// Directional Light
	XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);
	XMStoreFloat3(&mMainPassCB.Lights[0].Direction, lightDir);
	mMainPassCB.Lights[0].Strength = { 0.9f, 0.8f, 0.70f };
	
	// Point lights
	mMainPassCB.Lights[1].Strength = { 1.0f, 0.3f, 0.9f }; // 
	mMainPassCB.Lights[1].Position = { 12.5f, 0.0f, 30.0f };
	mMainPassCB.Lights[1].FalloffStart = 20.0f;
	mMainPassCB.Lights[1].FalloffEnd = 40.0f;

	mMainPassCB.Lights[2].Strength = { 1.0f, 1.0f, 1.0f }; // 
	mMainPassCB.Lights[2].Position = { 12.5f, 0.0f, 40.0f };
	mMainPassCB.Lights[2].FalloffStart = 20.0f;
	mMainPassCB.Lights[2].FalloffEnd = 40.0f;

	mMainPassCB.Lights[3].Strength = { 0.0f, 0.0f, 0.0f }; // 
	mMainPassCB.Lights[3].Position = { 12.5f, 0.0f, 50.0f };
	mMainPassCB.Lights[3].FalloffStart = 20.0f;
	mMainPassCB.Lights[3].FalloffEnd = 40.0f;

	mMainPassCB.Lights[4].Strength = { 1.0f, 0.0f, 0.0f }; // 
	mMainPassCB.Lights[4].Position = { 12.5f, 0.0f, 60.0f };
	mMainPassCB.Lights[4].FalloffStart = 20.0f;
	mMainPassCB.Lights[4].FalloffEnd = 40.0f;

	mMainPassCB.Lights[5].Strength = { 1.0f, 0.0f, 0.0f }; // 
	mMainPassCB.Lights[5].Position = { 12.5f, 0.0f, 70.0f };
	mMainPassCB.Lights[5].FalloffStart = 20.0f;
	mMainPassCB.Lights[5].FalloffEnd = 40.0f;

	// Spotlight
	mMainPassCB.Lights[6].Position = { mCam.GetPosition3f().x, mCam.GetPosition3f().y, mCam.GetPosition3f().z };
	mMainPassCB.Lights[6].Direction = { mCam.GetLook3f().x, mCam.GetLook3f().y, mCam.GetLook3f().z };
	mMainPassCB.Lights[6].FalloffStart = 30.0f; // full strength until reaching FalloffStart (distance terms from position)
	mMainPassCB.Lights[6].FalloffEnd = 60.0f; // strength decaying to zero while approaching FalloffEnd
	mMainPassCB.Lights[6].SpotPower = 25.0f; // S exponent indirectly controls angle intensity (shrink/expand cone)

	if (mFlashLight)
		mMainPassCB.Lights[6].Strength = { 1.0f, 1.0f, 1.0f }; // On
	else
		mMainPassCB.Lights[6].Strength = { 0.0f, 0.0f, 0.0f }; // Off

	// Fog
	if (mFog)
		mMainPassCB.gFogStart = 150.0f; // Less fog
	else
		mMainPassCB.gFogStart = 45.0f; // more fog

	
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void RasterizationApp::UpdateReflectedPassCB(const GameTimer& gt)
{

	// Supports only one moving light at the same time
	mReflectedPassCB = mMainPassCB;

	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	XMMATRIX R = XMMatrixReflect(mirrorPlane);

	// Reflect light sources in the mirror plane
	for (int i = 0; i < 8; ++i)
	{
		XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[i].Direction);
		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mReflectedPassCB.Lights[i].Direction, reflectedLightDir);
	}

	// Reflected pass stored in index 1
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(1, mReflectedPassCB);
}

void RasterizationApp::UpdateShadowPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mLightView);
	XMMATRIX proj = XMLoadFloat4x4(&mLightProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	UINT w = mShadowMap->Width();
	UINT h = mShadowMap->Height();

	XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mShadowPassCB.EyePosW = mLightPosW;
	mShadowPassCB.RenderTargetSize = XMFLOAT2((float)w, (float)h);
	mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
	mShadowPassCB.NearZ = mLightNearZ;
	mShadowPassCB.FarZ = mLightFarZ;

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(2, mShadowPassCB);
}

void RasterizationApp::UpdateShadowTransform(const GameTimer& gt)
{
	// Only the first "main" light casts a shadow.
	XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[0].Direction);
	XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

	XMStoreFloat3(&mLightPosW, lightPos);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	mLightNearZ = n;
	mLightFarZ = f;
	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = lightView * lightProj * T;
	XMStoreFloat4x4(&mLightView, lightView);
	XMStoreFloat4x4(&mLightProj, lightProj);
	XMStoreFloat4x4(&mShadowTransform, S);
}
