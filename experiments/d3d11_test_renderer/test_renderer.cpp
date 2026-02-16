#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <windows.h>
#include <iostream>
#include <chrono>
#include <format>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace std::chrono;

// Simple vertex structure
struct Vertex {
    float x, y, z;    // Position
    float r, g, b;    // Color
};

// Global variables
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain1> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_renderTargetView;
ComPtr<ID3D11VertexShader> g_vertexShader;
ComPtr<ID3D11PixelShader> g_pixelShader;
ComPtr<ID3D11Buffer> g_vertexBuffer;
ComPtr<ID3D11InputLayout> g_inputLayout;

HWND g_hwnd = nullptr;
bool g_running = true;
int g_frameCount = 0;
auto g_startTime = steady_clock::now();

// Simple vertex shader (transforms position)
const char* g_vertexShaderSource = R"(
struct VS_INPUT {
    float3 pos : POSITION;
    float3 color : COLOR;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float3 color : COLOR;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 1.0f);
    output.color = input.color;
    return output;
}
)";

// Simple pixel shader (outputs color)
const char* g_pixelShaderSource = R"(
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float3 color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET {
    return float4(input.color, 1.0f);
}
)";

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
            g_running = false;
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_running = false;
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

bool InitD3D11(HWND hwnd) {
    // Get window size
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // Create device and swap chain
    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = width;
    scd.Height = height;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags = 0;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        std::cerr << "Failed to create DXGI factory\n";
        return false;
    }

    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &g_device,
        &featureLevel,
        &g_context
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device\n";
        return false;
    }

    hr = factory->CreateSwapChainForHwnd(
        g_device.Get(),
        hwnd,
        &scd,
        nullptr,
        nullptr,
        &g_swapChain
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create swap chain\n";
        return false;
    }

    // Create render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_renderTargetView);

    // Set viewport
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);

    std::cout << std::format("[D3D11] Fullscreen borderless: {}x{}\n", width, height);

    return true;
}

bool InitShaders() {
    // Compile vertex shader
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        g_vertexShaderSource,
        strlen(g_vertexShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        0,
        0,
        &vsBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Vertex shader compile error: " << (char*)errorBlob->GetBufferPointer() << "\n";
        }
        return false;
    }

    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vertexShader);

    // Compile pixel shader
    ComPtr<ID3DBlob> psBlob;
    hr = D3DCompile(
        g_pixelShaderSource,
        strlen(g_pixelShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        0,
        0,
        &psBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Pixel shader compile error: " << (char*)errorBlob->GetBufferPointer() << "\n";
        }
        return false;
    }

    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pixelShader);

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    g_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_inputLayout);

    return true;
}

void InitGeometry() {
    // Create rotating square
    Vertex vertices[] = {
        { -0.5f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f }, // Top-left (red)
        {  0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 0.0f }, // Top-right (green)
        {  0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f }, // Bottom-right (blue)

        { -0.5f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f }, // Top-left (red)
        {  0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f }, // Bottom-right (blue)
        { -0.5f, -0.5f, 0.0f,  1.0f, 1.0f, 0.0f }, // Bottom-left (yellow)
    };

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(vertices);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = vertices;

    g_device->CreateBuffer(&bd, &initData, &g_vertexBuffer);
}

void Render(float time) {
    // Update rotating vertices
    float angle = time * 2.0f; // 2 radians per second
    float cosA = cosf(angle);
    float sinA = sinf(angle);

    Vertex vertices[] = {
        { -0.5f * cosA - 0.5f * sinA, -0.5f * -sinA + 0.5f * cosA, 0.0f,  1.0f, 0.0f, 0.0f },
        {  0.5f * cosA - 0.5f * sinA,  0.5f * -sinA + 0.5f * cosA, 0.0f,  0.0f, 1.0f, 0.0f },
        {  0.5f * cosA - -0.5f * sinA, 0.5f * -sinA + -0.5f * cosA, 0.0f,  0.0f, 0.0f, 1.0f },

        { -0.5f * cosA - 0.5f * sinA, -0.5f * -sinA + 0.5f * cosA, 0.0f,  1.0f, 0.0f, 0.0f },
        {  0.5f * cosA - -0.5f * sinA, 0.5f * -sinA + -0.5f * cosA, 0.0f,  0.0f, 0.0f, 1.0f },
        { -0.5f * cosA - -0.5f * sinA, -0.5f * -sinA + -0.5f * cosA, 0.0f,  1.0f, 1.0f, 0.0f },
    };

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    g_context->Map(g_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, vertices, sizeof(vertices));
    g_context->Unmap(g_vertexBuffer.Get(), 0);

    // Clear and render
    float clearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
    g_context->ClearRenderTargetView(g_renderTargetView.Get(), clearColor);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_context->IASetVertexBuffers(0, 1, g_vertexBuffer.GetAddressOf(), &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetInputLayout(g_inputLayout.Get());

    g_context->VSSetShader(g_vertexShader.Get(), nullptr, 0);
    g_context->PSSetShader(g_pixelShader.Get(), nullptr, 0);

    g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), nullptr);

    g_context->Draw(6, 0);

    g_swapChain->Present(1, 0); // VSync ON (60 FPS cap on 60Hz monitor, 100 FPS on 100Hz)

    g_frameCount++;
}

int main() {
    std::cout << "===========================================\n";
    std::cout << "  D3D11 Test Renderer\n";
    std::cout << "===========================================\n";
    std::cout << "This window will render a rotating square at your monitor's refresh rate.\n";
    std::cout << "Use this to test DXGI capture at full FPS.\n";
    std::cout << "Press ESC or close window to exit.\n\n";

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"D3D11TestRenderer";
    RegisterClassExW(&wc);

    // Get monitor resolution for fullscreen borderless
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);

    // Create fullscreen borderless window
    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST,  // Always on top
        L"D3D11TestRenderer",
        L"D3D11 Test Renderer - FULLSCREEN (Press ESC to exit)",
        WS_POPUP,  // Borderless
        0, 0,
        screen_width, screen_height,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    if (!g_hwnd) {
        std::cerr << "Failed to create window\n";
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // Initialize D3D11
    if (!InitD3D11(g_hwnd)) {
        return 1;
    }

    if (!InitShaders()) {
        return 1;
    }

    InitGeometry();

    std::cout << "Rendering started. FPS will be capped by VSync (monitor refresh rate).\n\n";

    // Main loop
    MSG msg{};
    auto lastFpsTime = steady_clock::now();
    int lastFrameCount = 0;

    while (g_running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        auto now = steady_clock::now();
        float time = duration<float>(now - g_startTime).count();

        Render(time);

        // Print FPS every second
        auto elapsed = duration<float>(now - lastFpsTime).count();
        if (elapsed >= 1.0f) {
            int framesDone = g_frameCount - lastFrameCount;
            float fps = framesDone / elapsed;
            std::cout << std::format("FPS: {:.1f} | Total frames: {}\n", fps, g_frameCount);

            lastFpsTime = now;
            lastFrameCount = g_frameCount;
        }
    }

    std::cout << "\nRenderer stopped. Total frames: " << g_frameCount << "\n";
    return 0;
}
