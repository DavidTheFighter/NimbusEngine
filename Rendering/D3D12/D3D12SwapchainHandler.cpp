/*
* MIT License
*
* Copyright (c) 2017 David Allen
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* D3D12SwapchainHandler.h
*
* Created on: Jul 5, 2018
*     Author: david
*/

#include "Rendering/D3D12/D3D12SwapchainHandler.h"

#include <Rendering/D3D12/D3D12Renderer.h>

#include <Resources/FileLoader.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

D3D12SwapchainHandler::D3D12SwapchainHandler(D3D12Renderer *rendererPtr)
{
	renderer = rendererPtr;
	swapchainUtilFenceValue = 0;

	UINT createFactoryFlags = 0;

	if (renderer->debugLayersEnabled)
		createFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

	DX_CHECK_RESULT(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	// Check for swapchain tearing support
	IDXGIFactory5 *dxgiFactory5 = nullptr;
	DX_CHECK_RESULT(dxgiFactory4->QueryInterface(&dxgiFactory5));

	BOOL swapchainSupportsTearing = FALSE;

	if (FAILED(dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &swapchainSupportsTearing, sizeof(swapchainSupportsTearing))))
		swapchainSupportsTearing = false;

	supportsTearing = bool(swapchainSupportsTearing);
	dxgiFactory5->Release();

	DX_CHECK_RESULT(renderer->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&swapchainUtilCommandAlloc)));
	DX_CHECK_RESULT(renderer->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&swapchainUtilFence)));
	swapchainUtilFenceEvent = createEventHandle();

	createRootSignature();
	createPSO();
	createConstantBuffer();
	createTexture();
	createVertexBuffer();
}

D3D12SwapchainHandler::~D3D12SwapchainHandler()
{
	for (auto swapIt = swapchains.begin(); swapIt != swapchains.end(); swapIt++)
	{
		uint64_t fenceValueForSignal = ++swapIt->second->swapchainFenceValue;
		DX_CHECK_RESULT(renderer->graphicsQueue->Signal(swapIt->second->swapchainFence, fenceValueForSignal));
		swapIt->second->bufferFenceValues[swapIt->second->currentBackBufferIndex] = fenceValueForSignal;

		swapIt->second->currentBackBufferIndex = swapIt->second->dxgiSwapchain4->GetCurrentBackBufferIndex();

		if (swapIt->second->swapchainFence->GetCompletedValue() < swapIt->second->bufferFenceValues[swapIt->second->currentBackBufferIndex])
		{
			DX_CHECK_RESULT(swapIt->second->swapchainFence->SetEventOnCompletion(swapIt->second->bufferFenceValues[swapIt->second->currentBackBufferIndex], swapIt->second->fenceEvent));
			WaitForSingleObject(swapIt->second->fenceEvent, static_cast<DWORD>(std::chrono::milliseconds::max().count()));
		}

		CloseHandle(swapIt->second->fenceEvent);
		swapIt->second->swapchainFence->Release();
		swapIt->second->descHeap->Release();
		swapIt->second->dxgiSwapchain4->Release();
		swapIt->second->cmdAllocator->Release();

		for (uint32_t c = 0; c < swapIt->second->cmdLists.size(); c++)
			swapIt->second->cmdLists[c]->Release();

		for (uint32_t bb = 0; bb < swapIt->second->backBuffers.size(); bb++)
			swapIt->second->backBuffers[bb]->Release();
	}

	swapchainUtilCommandAlloc->Release();
	swapchainUtilFence->Release();
	CloseHandle(swapchainUtilFenceEvent);

	swapchainPSO->Release();
	swapchainRootSig->Release();
	swapchainVertexBuffer->Release();

	swapchainDescHeap->Release();
	swapchainCBufferHeap->Release();
	swapchainTextureHeap->Release();

	dxgiFactory4->Release();
}

const uint32_t swapchainBufferCount = 3;

void D3D12SwapchainHandler::initSwapchain(Window *wnd)
{
	D3D12SwapchainData *swapchain = new D3D12SwapchainData();
	swapchain->parentWindow = wnd;
	swapchains[wnd] = swapchain;

	rtvDescriptorSize = renderer->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	createSwapchain(wnd);
}

void D3D12SwapchainHandler::prerecordSwapchainCommandList(D3D12SwapchainData *swapchain, ID3D12GraphicsCommandList *cmdList, uint32_t bufferIndex)
{
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(swapchain->backBuffers[bufferIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	cmdList->ResourceBarrier(1, &barrier);

	float clearColor[] = {0.4f, 0.6f, 0.9f, 1.0f};
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(swapchain->descHeap->GetCPUDescriptorHandleForHeapStart(), bufferIndex, rtvDescriptorSize);

	D3D12_RECT scissor = {0, 0, swapchain->parentWindow->getWidth(), swapchain->parentWindow->getHeight()};
	D3D12_VIEWPORT viewport = {0, 0, (float) swapchain->parentWindow->getWidth(), (float) swapchain->parentWindow->getHeight(), 0, 1};

	cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	cmdList->SetGraphicsRootSignature(swapchainRootSig);
	
	ID3D12DescriptorHeap *descHeaps[] = {swapchainDescHeap};
	cmdList->SetDescriptorHeaps(1, descHeaps);
	cmdList->SetGraphicsRootDescriptorTable(0, swapchainDescHeap->GetGPUDescriptorHandleForHeapStart());
	cmdList->SetPipelineState(swapchainPSO);
	cmdList->RSSetScissorRects(1, &scissor);
	cmdList->RSSetViewports(1, &viewport);
	cmdList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &swapchainVertexBufferView);
	cmdList->DrawInstanced(3, 1, 0, 0);

	barrier = CD3DX12_RESOURCE_BARRIER::Transition(swapchain->backBuffers[bufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	cmdList->ResourceBarrier(1, &barrier);

	DX_CHECK_RESULT(cmdList->Close());
}

void D3D12SwapchainHandler::presentToSwapchain(Window *wnd)
{
	D3D12SwapchainData *swapchain = swapchains[wnd];

	ID3D12CommandList *const cmdLists[] = {swapchain->cmdLists[swapchain->currentBackBufferIndex]};
	renderer->graphicsQueue->ExecuteCommandLists(1, cmdLists);

	UINT syncInterval = 0;
	UINT presentFlags = supportsTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;

	DX_CHECK_RESULT(swapchain->dxgiSwapchain4->Present(syncInterval, presentFlags));

	uint64_t fenceValueForSignal = ++swapchain->swapchainFenceValue;
	DX_CHECK_RESULT(renderer->graphicsQueue->Signal(swapchain->swapchainFence, fenceValueForSignal));
	swapchain->bufferFenceValues[swapchain->currentBackBufferIndex] = fenceValueForSignal;
	
	swapchain->currentBackBufferIndex = swapchain->dxgiSwapchain4->GetCurrentBackBufferIndex();

	if (swapchain->swapchainFence->GetCompletedValue() < swapchain->bufferFenceValues[swapchain->currentBackBufferIndex])
	{
		DX_CHECK_RESULT(swapchain->swapchainFence->SetEventOnCompletion(swapchain->bufferFenceValues[swapchain->currentBackBufferIndex], swapchain->fenceEvent));
		WaitForSingleObject(swapchain->fenceEvent, static_cast<DWORD>(std::chrono::milliseconds::max().count()));
	}
}

void D3D12SwapchainHandler::createSwapchain(Window *wnd)
{
	D3D12SwapchainData *swapchain = swapchains[wnd];

	swapchain->swapchainFenceValue = 0;
	swapchain->fenceEvent = createEventHandle();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = wnd->getWidth();
	swapchainDesc.Height = wnd->getHeight();
	swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.Stereo = FALSE;
	swapchainDesc.SampleDesc = {1, 0};
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = swapchainBufferCount;
	swapchainDesc.Scaling = DXGI_SCALING_NONE;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Flags = supportsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	HWND windowHandle = glfwGetWin32Window(static_cast<GLFWwindow*>(wnd->getWindowObjectPtr()));

	IDXGISwapChain1 *dxgiSwapchain1 = nullptr;
	DX_CHECK_RESULT(dxgiFactory4->CreateSwapChainForHwnd(renderer->graphicsQueue, windowHandle, &swapchainDesc, NULL, NULL, &dxgiSwapchain1));
	DX_CHECK_RESULT(dxgiSwapchain1->QueryInterface(&swapchain->dxgiSwapchain4));
	DX_CHECK_RESULT(dxgiFactory4->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER));
	dxgiSwapchain1->Release();

	swapchain->currentBackBufferIndex = swapchain->dxgiSwapchain4->GetCurrentBackBufferIndex();

	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.NumDescriptors = swapchainBufferCount;
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	DX_CHECK_RESULT(renderer->device->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&swapchain->descHeap)));

	UINT rtvDescriptorSize = renderer->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(swapchain->descHeap->GetCPUDescriptorHandleForHeapStart());

	for (uint32_t i = 0; i < swapchainBufferCount; i++)
	{
		ID3D12Resource *backBuffer = nullptr;

		DX_CHECK_RESULT(swapchain->dxgiSwapchain4->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
		renderer->device->CreateRenderTargetView(backBuffer, nullptr, rtvHandle);

		swapchain->backBuffers.push_back(backBuffer);
		rtvHandle.Offset(rtvDescriptorSize);
	}

	DX_CHECK_RESULT(renderer->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&swapchain->cmdAllocator)));

	for (uint32_t i = 0; i < swapchainBufferCount; i++)
	{
		ID3D12GraphicsCommandList *cmdList = nullptr;
		DX_CHECK_RESULT(renderer->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, swapchain->cmdAllocator, nullptr, IID_PPV_ARGS(&cmdList)));

		prerecordSwapchainCommandList(swapchain, cmdList, i);

		swapchain->cmdLists.push_back(cmdList);
		swapchain->bufferFenceValues.push_back(0);
	}

	DX_CHECK_RESULT(renderer->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&swapchain->swapchainFence)));
}

void D3D12SwapchainHandler::destroySwapchain(Window *wnd)
{
	D3D12SwapchainData *swapchain = swapchains[wnd];

	uint64_t fenceValueForSignal = ++swapchain->swapchainFenceValue;
	DX_CHECK_RESULT(renderer->graphicsQueue->Signal(swapchain->swapchainFence, fenceValueForSignal));
	swapchain->bufferFenceValues[swapchain->currentBackBufferIndex] = fenceValueForSignal;

	swapchain->currentBackBufferIndex = swapchain->dxgiSwapchain4->GetCurrentBackBufferIndex();

	if (swapchain->swapchainFence->GetCompletedValue() < swapchain->bufferFenceValues[swapchain->currentBackBufferIndex])
	{
		DX_CHECK_RESULT(swapchain->swapchainFence->SetEventOnCompletion(swapchain->bufferFenceValues[swapchain->currentBackBufferIndex], swapchain->fenceEvent));
		WaitForSingleObject(swapchain->fenceEvent, static_cast<DWORD>(std::chrono::milliseconds::max().count()));
	}

	CloseHandle(swapchain->fenceEvent);
	swapchain->swapchainFence->Release();
	swapchain->descHeap->Release();
	swapchain->dxgiSwapchain4->Release();
	swapchain->cmdAllocator->Release();

	for (uint32_t c = 0; c < swapchain->cmdLists.size(); c++)
		swapchain->cmdLists[c]->Release();

	for (uint32_t bb = 0; bb < swapchain->backBuffers.size(); bb++)
		swapchain->backBuffers[bb]->Release();
}

void D3D12SwapchainHandler::recreateSwapchain(Window *wnd)
{
	D3D12SwapchainData *swapchain = swapchains[wnd];

	for (uint32_t i = 0; i < swapchain->backBuffers.size(); i++)
	{
		swapchain->backBuffers[i]->Release();
		swapchain->bufferFenceValues[i] = swapchain->bufferFenceValues[swapchain->currentBackBufferIndex];
		swapchain->cmdLists[i]->Release();
	}

	swapchain->backBuffers.clear();
	swapchain->cmdLists.clear();

	DXGI_SWAP_CHAIN_DESC swapchainDesc = {};
	DX_CHECK_RESULT(swapchain->dxgiSwapchain4->GetDesc(&swapchainDesc));
	DX_CHECK_RESULT(swapchain->dxgiSwapchain4->ResizeBuffers((UINT) swapchain->bufferFenceValues.size(), wnd->getWidth(), wnd->getHeight(), swapchainDesc.BufferDesc.Format, swapchainDesc.Flags));

	swapchain->currentBackBufferIndex = swapchain->dxgiSwapchain4->GetCurrentBackBufferIndex();

	UINT rtvDescriptorSize = renderer->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(swapchain->descHeap->GetCPUDescriptorHandleForHeapStart());

	for (uint32_t i = 0; i < (uint32_t) swapchain->bufferFenceValues.size(); i++)
	{
		ID3D12Resource *backBuffer = nullptr;

		DX_CHECK_RESULT(swapchain->dxgiSwapchain4->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
		renderer->device->CreateRenderTargetView(backBuffer, nullptr, rtvHandle);

		swapchain->backBuffers.push_back(backBuffer);
		rtvHandle.Offset(rtvDescriptorSize);
	}

	for (uint32_t i = 0; i < (uint32_t) swapchain->bufferFenceValues.size(); i++)
	{
		ID3D12GraphicsCommandList *cmdList = nullptr;
		DX_CHECK_RESULT(renderer->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, swapchain->cmdAllocator, nullptr, IID_PPV_ARGS(&cmdList)));

		prerecordSwapchainCommandList(swapchain, cmdList, i);

		swapchain->cmdLists.push_back(cmdList);
	}
}

void D3D12SwapchainHandler::createRootSignature()
{
	D3D12_DESCRIPTOR_RANGE descRanges[2];
	descRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descRanges[0].NumDescriptors = 1;
	descRanges[0].BaseShaderRegister = 0;
	descRanges[0].RegisterSpace = 0;
	descRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	descRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descRanges[1].NumDescriptors = 1;
	descRanges[1].BaseShaderRegister = 1;
	descRanges[1].RegisterSpace = 0;
	descRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_DESCRIPTOR_TABLE descTable = {};
	descTable.NumDescriptorRanges = sizeof(descRanges) / sizeof(descRanges[0]);
	descTable.pDescriptorRanges = descRanges;

	std::vector<D3D12_ROOT_PARAMETER> rootParams(1);
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[0].DescriptorTable = descTable;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.MipLODBias = 0;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = 1;
	samplerDesc.ShaderRegister = 2;
	samplerDesc.RegisterSpace = 0;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.NumParameters = rootParams.size();
	rootSigDesc.pParameters = rootParams.data();
	rootSigDesc.NumStaticSamplers = 1;
	rootSigDesc.pStaticSamplers = &samplerDesc;
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;

	ID3DBlob* signature = nullptr;
	DX_CHECK_RESULT(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr));
	DX_CHECK_RESULT(renderer->device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&swapchainRootSig)));

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 2;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	DX_CHECK_RESULT(renderer->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&swapchainDescHeap)));
}

void D3D12SwapchainHandler::createConstantBuffer()
{
	DX_CHECK_RESULT(renderer->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(1024 * 64), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&swapchainCBufferHeap)));
	swapchainCBufferHeap->SetName(L"CBuffer Upload Resource Heap");

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = swapchainCBufferHeap->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (sizeof(glm::vec4) + 255) & ~255;
	renderer->device->CreateConstantBufferView(&cbvDesc, swapchainDescHeap->GetCPUDescriptorHandleForHeapStart());

	uint8_t *swapchainCBUfferGPUAddress = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	DX_CHECK_RESULT(swapchainCBufferHeap->Map(0, &readRange, reinterpret_cast<void**>(&swapchainCBUfferGPUAddress)));

	glm::vec4 dat(1, 0.5f, 1, 1);
	memcpy(swapchainCBUfferGPUAddress, &dat, sizeof(dat));
}

void D3D12SwapchainHandler::createTexture()
{
	//swapchainTextureHeap

	const uint32_t texWidth = 16, texHeight = 16;
	uint8_t texData[texWidth * texHeight * 4];

	for (uint32_t x = 0; x < texWidth; x++)
	{
		for (uint32_t y = 0; y < texHeight; y++)
		{
			texData[x * texHeight * 4 + y * 4] = 0;
			texData[x * texHeight * 4 + y * 4 + 1] = 0;
			texData[x * texHeight * 4 + y * 4 + 2] = uint8_t((x / float(texWidth - 1)) * 255);
			texData[x * texHeight * 4 + y * 4 + 3] = 255;
		}
	}

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = texWidth;
	texDesc.Height = texHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc = {1, 0};
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	DX_CHECK_RESULT(renderer->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&swapchainTextureHeap)));
	swapchainTextureHeap->SetName(L"Texture Resource Heap");

	//int textureHeapSize = ((((texWidth * 4) + 255) & ~255) * (texHeight - 1)) + (texWidth * 4);

	UINT64 textureUploadBufferSize;
	renderer->device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &textureUploadBufferSize);

	ID3D12Resource *textureUploadHeap = nullptr;

	DX_CHECK_RESULT(renderer->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureUploadHeap)));
	textureUploadHeap->SetName(L"Texture Upload Resource Heap");

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = reinterpret_cast<BYTE*>(texData);
	textureData.RowPitch = 4 * texWidth;
	textureData.SlicePitch = 4 * texWidth * texHeight;

	ID3D12GraphicsCommandList *cmdList = nullptr;
	DX_CHECK_RESULT(renderer->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, swapchainUtilCommandAlloc, nullptr, IID_PPV_ARGS(&cmdList)));

	UpdateSubresources(cmdList, swapchainTextureHeap, textureUploadHeap, 0, 0, 1, &textureData);
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(swapchainTextureHeap, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	DX_CHECK_RESULT(cmdList->Close());

	ID3D12CommandList *const cmdLists[] = {cmdList};

	renderer->graphicsQueue->ExecuteCommandLists(1, cmdLists);

	swapchainUtilFenceValue++;
	DX_CHECK_RESULT(renderer->graphicsQueue->Signal(swapchainUtilFence, swapchainUtilFenceValue));

	if (swapchainUtilFence->GetCompletedValue() < swapchainUtilFenceValue)
	{
		DX_CHECK_RESULT(swapchainUtilFence->SetEventOnCompletion(swapchainUtilFenceValue, swapchainUtilFenceEvent));
		WaitForSingleObject(swapchainUtilFenceEvent, static_cast<DWORD>(std::chrono::milliseconds::max().count()));
	}

	cmdList->Release();
	textureUploadHeap->Release();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;

	uint32_t srvUavDescriptorSize = renderer->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);;
	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(swapchainDescHeap->GetCPUDescriptorHandleForHeapStart(), 1, srvUavDescriptorSize);

	renderer->device->CreateShaderResourceView(swapchainTextureHeap, &srvDesc, srvHandle);
}

void D3D12SwapchainHandler::createVertexBuffer()
{
	glm::vec3 vertices[] = {
		glm::vec3(0, 0.5f, 0.5f),
		glm::vec3(0.5f, -0.5f, 0.5f),
		glm::vec3(-0.5f, -0.5f, 0.5f)
	};

	DX_CHECK_RESULT(renderer->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices)), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&swapchainVertexBuffer)));
	swapchainVertexBuffer->SetName(L"Swapchain Vertex Buffer Resource Heap");

	ID3D12Resource *vertexBufferUploadHeap = nullptr;
	DX_CHECK_RESULT(renderer->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices)), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBufferUploadHeap)));
	vertexBufferUploadHeap->SetName(L"Swapchain Vertex Buffer Upload Heap");

	D3D12_SUBRESOURCE_DATA vertexData = {};
	vertexData.pData = reinterpret_cast<BYTE*>(vertices);
	vertexData.RowPitch = sizeof(vertices);
	vertexData.SlicePitch = sizeof(vertices);

	ID3D12GraphicsCommandList *cmdList = nullptr;
	DX_CHECK_RESULT(renderer->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, swapchainUtilCommandAlloc, nullptr, IID_PPV_ARGS(&cmdList)));

	UpdateSubresources(cmdList, swapchainVertexBuffer, vertexBufferUploadHeap, 0, 0, 1, &vertexData);
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(swapchainVertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
	DX_CHECK_RESULT(cmdList->Close());

	ID3D12CommandList *const cmdLists[] = {cmdList};

	renderer->graphicsQueue->ExecuteCommandLists(1, cmdLists);

	swapchainUtilFenceValue++;
	DX_CHECK_RESULT(renderer->graphicsQueue->Signal(swapchainUtilFence, swapchainUtilFenceValue));

	if (swapchainUtilFence->GetCompletedValue() < swapchainUtilFenceValue)
	{
		DX_CHECK_RESULT(swapchainUtilFence->SetEventOnCompletion(swapchainUtilFenceValue, swapchainUtilFenceEvent));
		WaitForSingleObject(swapchainUtilFenceEvent, static_cast<DWORD>(std::chrono::milliseconds::max().count()));
	}

	swapchainVertexBufferView.BufferLocation = swapchainVertexBuffer->GetGPUVirtualAddress();
	swapchainVertexBufferView.StrideInBytes = sizeof(glm::vec3);
	swapchainVertexBufferView.SizeInBytes = sizeof(vertices);

	cmdList->Release();
	vertexBufferUploadHeap->Release();
}

void D3D12SwapchainHandler::createPSO()
{
	ID3DBlob *vertexShader = nullptr, *pixelShader = nullptr;
	ID3DBlob *errorBuf = nullptr;

	std::vector<char> shaderSrc = FileLoader::instance()->readFileBuffer("GameData/shaders/d3d12/swapchain.hlsl");

	HRESULT hr = D3DCompile(shaderSrc.data(), shaderSrc.size(), ".../d3d12/swapchain.hlsl(vs)", nullptr, nullptr, "SwapchainVS", "vs_5_0", D3DCOMPILE_DEBUG | D3DCOMPILE_ALL_RESOURCES_BOUND | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &vertexShader, &errorBuf);

	if (FAILED(hr))
	{
		printf("%s Failed to compile the vertex shader for swapchains. Error msg: \n%s\n", ERR_PREFIX, (char*) errorBuf->GetBufferPointer());
		throw std::runtime_error("d3d12 shader compile error");
	}

	hr = D3DCompile(shaderSrc.data(), shaderSrc.size(), ".../d3d12/swapchain.hlsl(ps)", nullptr, nullptr, "SwapchainPS", "ps_5_0", D3DCOMPILE_DEBUG | D3DCOMPILE_ALL_RESOURCES_BOUND | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &pixelShader, &errorBuf);

	if (FAILED(hr))
	{
		printf("%s Failed to compile the pixel shader for swapchains. Error msg: \n%s\n", ERR_PREFIX, (char*) errorBuf->GetBufferPointer());
		throw std::runtime_error("d3d12 shader compile error");
	}

	D3D12_SHADER_BYTECODE vertexShaderBytecode = {}, pixelShaderBytecode = {};
	vertexShaderBytecode.BytecodeLength = vertexShader->GetBufferSize();
	vertexShaderBytecode.pShaderBytecode = vertexShader->GetBufferPointer();
	pixelShaderBytecode.BytecodeLength = pixelShader->GetBufferSize();
	pixelShaderBytecode.pShaderBytecode = pixelShader->GetBufferPointer();

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
	inputLayoutDesc.NumElements = sizeof(inputLayout) / sizeof(inputLayout[0]);
	inputLayoutDesc.pInputElementDescs = inputLayout;
	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = swapchainRootSig;
	psoDesc.VS = vertexShaderBytecode;
	psoDesc.PS = pixelShaderBytecode;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = 0xFFFFFF;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.InputLayout = inputLayoutDesc;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc = {1, 0};

	DX_CHECK_RESULT(renderer->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&swapchainPSO)));
}