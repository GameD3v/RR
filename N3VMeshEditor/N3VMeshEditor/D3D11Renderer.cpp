// D3D11Renderer.cpp
#include "D3D11Renderer.h"              // Kendi başlık dosyamızı include et
#include <cstdio>                       // fprintf için
#include <exception>                    // std::exception için
#include <string>                       // std::string için (artık std::wstring de kullanacağız)
#include <Windows.h>                    // HANDLE, CreateFileW, GetLastError, FormatMessageW için
#include <QDebug>                       // qDebug için
#include <QMessageBox>
#include <vector>                       // std::vector için
#include <d3dcompiler.h>                // D3DCompile fonksiyonu ve bayrakları için
#pragma comment(lib, "d3dcompiler.lib") // D3DCompile fonksiyonu için gerekli kütüphaneyi otomatik bağlar.
#include <DirectXColors.h>              // Colors::MidnightBlue için
#include <algorithm>                    // std::min ve std::max için (önceki versiyonlarda eklendi)
#include <cmath>                        // tanf, sqrtf için (önceki versiyonlarda eklendi)

// DirectXMath kütüphanesini kullanmak için bu namespace'i import ediyoruz
using namespace DirectX;

// Basic Vertex Shader HLSL kodu (RAW STRING LITERAL KULLANMADAN)
const char* g_VertexShader =
"cbuffer ConstantBuffer : register(b0)\n"
"{\n"
"    matrix World;\n"
"    matrix View;\n"
"    matrix Projection;\n"
"};\n"
"\n"
"struct VS_INPUT\n"
"{\n"
"    float3 Pos : POSITION;\n"
"    float4 Color : COLOR;\n"
"};\n"
"\n"
"struct PS_INPUT\n"
"{\n"
"    float4 Pos : SV_POSITION;\n"
"    float4 Color : COLOR;\n"
"};\n"
"\n"
"PS_INPUT VSMain(VS_INPUT input)\n"
"{\n"
"    PS_INPUT output;\n"
"    output.Pos = mul(float4(input.Pos, 1.0f), World);\n"
"    output.Pos = mul(output.Pos, View);\n"
"    output.Pos = mul(output.Pos, Projection);\n"
"    output.Color = input.Color;\n"
"    return output;\n"
"}\n";

// Güncellenmiş Pixel Shader HLSL kodu (AYNI KALIYOR, RENK MANTIĞI DOĞRU)
const char* g_PixelShader =
"cbuffer ConstantBuffer : register(b0)\n"
"{\n"
"    matrix World;\n"
"    matrix View;\n"
"    matrix Projection;\n"
"    int RenderMode; // 0: Solid, 1: Kırmızı Tel Kafes, 2: Yeşil Tel Kafes\n"
"};\n"
"\n"
"struct PS_INPUT\n"
"{\n"
"    float4 Pos : SV_POSITION;\n"
"    float4 Color : COLOR;\n"
"};\n"
"\n"
"float4 PSMain(PS_INPUT input) : SV_TARGET\n"
"{\n"
"    if (RenderMode == 1) // Kırmızı Tel Kafes modu (Collision Mesh)\n"
"    {\n"
"        return float4(1.0f, 0.0f, 0.0f, 1.0f); // Kırmızı\n"
"    }\n"
"    else if (RenderMode == 2) // Parlak Yeşil Tel Kafes modu (Seçim Vurgusu)\n"
"    {\n"
"        return float4(0.0f, 1.0f, 0.0f, 1.0f); // Parlak Yeşil\n"
"    }\n"
"    else // RenderMode == 0 (Solid veya Normal)\n"
"    {\n"
"        return input.Color; // Normal vertex rengini kullan\n"
"    }\n"
"}\n";

D3D11Renderer::D3D11Renderer()
    : m_width(0), 
    m_height(0),
    m_cameraPos(0.0f, 0.0f, -10.0f), // Başlangıç kamera pozisyonu
    m_cameraTarget(0.0f, 0.0f, 0.0f), // Başlangıç kamera hedefi
    m_cameraUp(0.0f, 1.0f, 0.0f),     // Kameranın yukarı yönü
    m_cameraRadius(10.0f),           // Kamera hedefinden uzaklık, başlangıçta 10 birim
    m_yaw(0.0f),                     // Kamera yatay dönüş açısı (radyan)
    m_pitch(0.0f),                   // Kamera dikey dönüş açısı (radyan)
    m_zoomSpeed(0.1f),               // Yakınlaştırma/uzaklaştırma hızı
    m_mouseSpeedX(0.01f),            // Fare X hareketi kamera dönüş hızı
    m_mouseSpeedY(0.01f),            // Fare Y hareketi kamera dönüş hızı
    m_isOrbiting(false),             // Fare ile döndürme modunda mı? 
    m_wireframeMode(false),           // Kafes modu başlangıçta kapalı
    m_isMeshSelected(false), // Başlangıçta mesh seçili değil
    m_worldTranslation(0.0f, 0.0f, 0.0f), // Mesh'in başlangıçtaki dünya pozisyonu
    m_selectedMeshInitialWorldPos(0.0f, 0.0f, 0.0f), // YENİ
    m_initialMouseWorldPos(0.0f, 0.0f, 0.0f),      // YENİ
    m_gridVertexCount(0) // Yeni üye değişkeni başlangıç değeri
{
    // Matrisleri sıfırla
    m_worldMatrix = XMMatrixIdentity();
    m_viewMatrix = XMMatrixIdentity();
    m_projectionMatrix = XMMatrixIdentity();

    // Kamera pozisyonu, hedef ve uzaklık kullanılarak SetupCamera() içinde ayarlanacak.
    // m_cameraPos burada doğrudan ayarlanmamalı, SetupCamera'ya bırakılmalı.
}

D3D11Renderer::~D3D11Renderer()
{
    Shutdown(); // Kaynakları temizle
}

//void D3D11Renderer::Shutdown()
//{
//    // DirectX nesneleri ComPtr tarafından otomatik olarak serbest bırakılır.
//    // Gerekirse ek temizleme işlemleri buraya eklenebilir.
//}

bool D3D11Renderer::CompileShader(const char* shaderCode, const char* entryPoint, const char* profile, ID3DBlob** blob)
{
    HRESULT hr = D3DCompile(shaderCode,
        strlen(shaderCode),
        nullptr,
        nullptr,
        nullptr,
        entryPoint,
        profile,
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        blob,
        nullptr);
    if (FAILED(hr)) {
        // Hata mesajını daha detaylı logla
        if (*blob) {
            qDebug() << "Shader Compile Error (" << entryPoint << ":" << profile << "):\n" << (char*)(*blob)->GetBufferPointer();
        }
        else {
            qDebug() << "Hata: Shader derlenirken hata! EntryPoint: " << entryPoint << ", Profile: " << profile << ", HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        }
        return false;
    }
    return true;
}

bool D3D11Renderer::Initialize(HWND hWnd, int width, int height)
{
    m_width = width;
    m_height = height;

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0; // <<--- DÜZELTME: SampleDesc.Quality
    scd.Windowed = TRUE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &scd, m_swapChain.GetAddressOf(),
        m_d3dDevice.GetAddressOf(), nullptr, m_d3dContext.GetAddressOf());

    if (FAILED(hr)) {
        qDebug() << "Hata: D3D11 Cihaz ve Swap Chain olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Swap Chain'den Back Buffer alinirken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Render Target View olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = width;
    depthStencilDesc.Height = height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0; // <<--- DÜZELTME: SampleDesc.Quality
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthStencilDesc.CPUAccessFlags = 0;
    depthStencilDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencilBuffer;
    hr = m_d3dDevice->CreateTexture2D(&depthStencilDesc, nullptr, depthStencilBuffer.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Derinlik Stencil Buffer olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = depthStencilDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = m_d3dDevice->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Derinlik Stencil View olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    if (!CompileShader(g_VertexShader, "VSMain", "vs_5_0", m_vertexShaderBlob.GetAddressOf())) return false;
    hr = m_d3dDevice->CreateVertexShader(m_vertexShaderBlob->GetBufferPointer(), m_vertexShaderBlob->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf());
    if (FAILED(hr)) { qDebug() << "Hata: Vertex Shader olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0')); return false; }

    Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;
    if (!CompileShader(g_PixelShader, "PSMain", "ps_5_0", pixelShaderBlob.GetAddressOf())) return false;
    hr = m_d3dDevice->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, m_pixelShader.GetAddressOf());
    if (FAILED(hr)) { qDebug() << "Hata: Pixel Shader olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0')); return false; }

    if (!CreateBuffers()) return false;
    if (!CreateRasterizerStates()) return false; // Yeni: Rasterizer State'leri oluştur

    // Zemin ızgarası buffer'larını oluşturma çağrısı
    // 100x100 birimlik bir alan, her birim 1x1 metre boyutunda olsun.
    CreateGridBuffers(2000.0f, 100);

    SetupCamera(); // Başlangıç kamera pozisyonunu ayarla

    return true;
}

void D3D11Renderer::Shutdown()
{
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_constantBuffer.Reset();
    m_inputLayout.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_vertexShaderBlob.Reset();
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();
    m_solidRasterizerState.Reset(); // Yeni: Solid rasterizer state'i temizle
    m_wireframeRasterizerState.Reset(); // Yeni: Wireframe rasterizer state'i temizle
    m_swapChain.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();

    m_collisionMesh.Release();
}

void D3D11Renderer::Resize(int width, int height)
{
    if (!m_d3dContext || !m_swapChain || width == 0 || height == 0) return;

    m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        qDebug() << "Hata: Swap Chain yeniden boyutlandirilirken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Swap Chain'den Back Buffer alinirken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return;
    }
    hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Render Target View olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return;
    }

    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = width;
    depthStencilDesc.Height = height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0; // <<--- DÜZELTME: SampleDesc.Quality
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthStencilDesc.CPUAccessFlags = 0;
    depthStencilDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencilBuffer;
    hr = m_d3dDevice->CreateTexture2D(&depthStencilDesc, nullptr, depthStencilBuffer.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Derinlik Stencil Buffer olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = depthStencilDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = m_d3dDevice->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Derinlik Stencil View olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return;
    }

    m_width = width;
    m_height = height;

    // Projection matrisi yeniden boyutlandırmada güncellenmeli
    m_projectionMatrix = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_width / (float)m_height, 0.01f, 1000.0f);
}

void D3D11Renderer::Render()
{
    if (!m_d3dContext || !m_swapChain) return;

    const float clearColor[4] = { 61.0f / 255.0f, 61.0f / 255.0f, 61.0f / 255.0f, 1.0f };
    m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
    m_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

    D3D11_VIEWPORT vp = { 0 };
    vp.Width = (FLOAT)m_width;
    vp.Height = (FLOAT)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_d3dContext->RSSetViewports(1, &vp);

    // Kamera matrislerini güncelle
    SetupCamera();

    // Zemin ızgarasını çiz (her zaman görünür)
    DrawGrid();

    // Mesh için genel ayarlar
    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // Üçgen listesi olarak çiz
    m_d3dContext->IASetInputLayout(m_inputLayout.Get());
    m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    // --- Mesh Çizimi ---
    if (m_vertexBuffer && m_collisionMesh.VertexCount() > 0)
    {
        UINT stride = sizeof(__VertexColor); // Vertex yapınızın boyutu
        UINT offset = 0;
        m_d3dContext->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
        if (m_indexBuffer && m_collisionMesh.IndexCount() > 0)
        {
            m_d3dContext->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        }

        // Dünya matrisini ayarla
        m_worldMatrix = DirectX::XMMatrixTranslation(m_worldTranslation.x, m_worldTranslation.y, m_worldTranslation.z);

        // Constant Buffer verilerini hazırla
        ConstantBufferData cbData;
        cbData.World = DirectX::XMMatrixTranspose(m_worldMatrix);
        cbData.View = DirectX::XMMatrixTranspose(m_viewMatrix);
        cbData.Projection = DirectX::XMMatrixTranspose(m_projectionMatrix);

        // --- RASTERIZER STATE VE RENDER MODE AYARLAMA MANTIĞI ---
        // Seçim durumu, wireframe modundan önceliklidir.
        if (m_isMeshSelected)
        {
            // Eğer mesh seçiliyse: YEŞİL TEL KAFES (öncelikli)
            m_d3dContext->RSSetState(m_wireframeRasterizerState.Get()); // Tel kafes görünümünü zorla
            cbData.RenderMode = 2; // Pixel Shader'da yeşil rengi seç
        }
        else if (m_wireframeMode)
        {
            // Eğer genel kafes modu aktifse (ve mesh seçili değilse): KIRMIZI TEL KAFES
            m_d3dContext->RSSetState(m_wireframeRasterizerState.Get()); // Tel kafes görünümünü zorla
            cbData.RenderMode = 1; // Pixel Shader'da kırmızı rengi seç
        }
        else
        {
            // Normal durum: SOLID ve objenin kendi rengi
            m_d3dContext->RSSetState(m_solidRasterizerState.Get()); // Dolu (solid) görünüm
            cbData.RenderMode = 0; // Pixel Shader'da objenin kendi rengini kullan
        }

        // Sabit tamponu güncelle ve shader'a gönder
        m_d3dContext->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cbData, 0, 0);
        m_d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        m_d3dContext->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

        // Mesh'i çiz
        if (m_indexBuffer && m_collisionMesh.IndexCount() > 0) {
            m_d3dContext->DrawIndexed(m_collisionMesh.IndexCount(), 0, 0);
        }
        else {
            m_d3dContext->Draw(m_collisionMesh.VertexCount(), 0);
        }
    }

    HRESULT hr = m_swapChain->Present(1, 0);
    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            qDebug() << "Hata: DirectX cihazi kayboldu veya sifirlandi! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        }
        else
        {
            qDebug() << "Hata: Swap Chain Present hatasi! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        }
    }
}

// D3D11Renderer::CreateGridBuffers fonksiyonunun implementasyonu
void D3D11Renderer::CreateGridBuffers(float size, int subdivisions)
{
    float halfSize = size / 2.0f;
    float step = size / static_cast<float>(subdivisions);

    std::vector<GridVertex> vertices;
    vertices.reserve((subdivisions + 1) * 4); // Dikey ve yatay çizgiler için tahmini boyut

    // Çizgi renkleri
    DirectX::XMFLOAT4 minorColor = DirectX::XMFLOAT4(80.0f / 255.0f, 80.0f / 255.0f, 80.0f / 255.0f, 1.0f); // RGB 80,80,80 (opak)
    DirectX::XMFLOAT4 majorColor = DirectX::XMFLOAT4(100.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f); // RGB 100,100,100 (opak, daha belirgin)
    DirectX::XMFLOAT4 axisColor = DirectX::XMFLOAT4(150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f); // RGB 150,150,150 (daha açık gri, eksenler)
    DirectX::XMFLOAT4 axisZColor = DirectX::XMFLOAT4(0.0f / 255.0f, 0.0f / 255.0f, 255.0f / 255.0f, 1.0f);  // Mavi Z ekseni (örn.)

    // Ana çizgi sıklığı: Örneğin her 10 alt bölmede bir ana çizgi olsun
    // Bu değeri değiştirerek ana çizgilerin ne sıklıkla çizileceğini ayarlayabilirsiniz.
    int majorLineInterval = 10;

    // Dikey çizgiler (Z ekseni boyunca, X sabit)
    for (int i = 0; i <= subdivisions; ++i) {
        float x = -halfSize + i * step;
        DirectX::XMFLOAT4 color = minorColor;

        // Ortadaki çizgi (x=0) için özel renk, eğer subdivisions çift ise tam ortadaki çizgiye denk gelir.
        // Tek ise, orta iki çizgiden biri daha koyu olabilir.
        // Ana eksen çizgisi (X = 0)
        if (fabs(x) < 0.001f) { // Float karşılaştırması için küçük bir tolerans
            color = axisColor;
        }
        else if (i % majorLineInterval == 0) {
            color = majorColor;
        }


        vertices.push_back({ DirectX::XMFLOAT3(x, 0.0f, -halfSize), color });
        vertices.push_back({ DirectX::XMFLOAT3(x, 0.0f, halfSize), color });
    }

    // Yatay çizgiler (X ekseni boyunca, Z sabit)
    for (int i = 0; i <= subdivisions; ++i) {
        float z = -halfSize + i * step;
        DirectX::XMFLOAT4 color = minorColor;

        // Ana eksen çizgisi (Z = 0)
        if (fabs(z) < 0.001f) { // Float karşılaştırması için küçük bir tolerans
            color = axisColor;
        }
        // Ana çizgiler
        else if (i % majorLineInterval == 0) {
            color = majorColor;
        }

        vertices.push_back({ DirectX::XMFLOAT3(-halfSize, 0.0f, z), color });
        vertices.push_back({ DirectX::XMFLOAT3(halfSize, 0.0f, z), color });
    }

    m_gridVertexCount = static_cast<UINT>(vertices.size());

    // Vertex Buffer oluşturma
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(GridVertex) * m_gridVertexCount;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices.data();

    HRESULT hr = m_d3dDevice->CreateBuffer(&bd, &initData, m_gridVertexBuffer.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Grid Vertex Buffer oluşturulamadı! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
    }
}

// D3D11Renderer::DrawGrid fonksiyonunun implementasyonu
void D3D11Renderer::DrawGrid()
{
    if (!m_gridVertexBuffer) { // Buffer oluşturulmadıysa çizme
        return;
    }

    // Izgara için shader'ları ve input layout'u ayarla
    m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0); // Model shader'ı kullanıyoruz
    m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);   // Model shader'ı kullanıyoruz
    m_d3dContext->IASetInputLayout(m_inputLayout.Get());         // Model input layout'u kullanıyoruz

    UINT stride = sizeof(GridVertex);
    UINT offset = 0;
    m_d3dContext->IASetVertexBuffers(0, 1, m_gridVertexBuffer.GetAddressOf(), &stride, &offset);

    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST); // Çizgi listesi olarak çiz

    // Constant buffer'ı güncelle (View ve Projection matrisleri)
    DirectX::XMMATRIX world = DirectX::XMMatrixIdentity(); // Izgara için dünya matrisi birim matris

    ConstantBufferData cb; // ConstantBufferData yapısı
    cb.World = DirectX::XMMatrixTranspose(world);
    cb.View = DirectX::XMMatrixTranspose(m_viewMatrix);      // m_viewMatrix kullanıldı
    cb.Projection = DirectX::XMMatrixTranspose(m_projectionMatrix); // m_projectionMatrix kullanıldı

    m_d3dContext->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    m_d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_d3dContext->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf()); // Eğer pixel shader'da da kullanıyorsanız

    // Izgarayı çiz
    m_d3dContext->Draw(m_gridVertexCount, 0);
}

bool D3D11Renderer::LoadMesh(const std::wstring& filePath)
{
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        LPWSTR messageBuffer = nullptr;
        size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, NULL);

        qDebug() << "Hata: Mesh dosyasi acilamadi: " << QString::fromStdWString(filePath) << "(Hata Kodu: " << error << " - " << (messageBuffer ? QString::fromWCharArray(messageBuffer) : "Bilinmeyen hata") << ")";

        if (messageBuffer) LocalFree(messageBuffer);
        return false;
    }

    if (!m_collisionMesh.Load(hFile))
    {
        CloseHandle(hFile);
        qDebug() << "Hata: Mesh dosyasi yuklenemedi: " << QString::fromStdWString(filePath);
        return false;
    }
    CloseHandle(hFile);

    if (m_collisionMesh.VertexCount() == 0) {
        qDebug() << "Hata: Yuklenen mesh'te hic vertex yok: " << QString::fromStdWString(filePath);
        return false;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = m_collisionMesh.VertexCount() * sizeof(__VertexColor);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;
    bd.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA InitData = {};
    InitData.pSysMem = m_collisionMesh.GetVertices();

    HRESULT hr = m_d3dDevice->CreateBuffer(&bd, &InitData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        // fprintf yerine qDebug kullanın
        qDebug() << "Hata: Vertex Buffer olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    if (m_collisionMesh.IndexCount() > 0)
    {
        D3D11_BUFFER_DESC ibd = {};
        ibd.Usage = D3D11_USAGE_DEFAULT;
        ibd.ByteWidth = m_collisionMesh.IndexCount() * sizeof(WORD);
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibd.CPUAccessFlags = 0;
        ibd.MiscFlags = 0;
        ibd.StructureByteStride = 0;

        D3D11_SUBRESOURCE_DATA InitIndexData = {};
        InitIndexData.pSysMem = m_collisionMesh.GetIndices();

        hr = m_d3dDevice->CreateBuffer(&ibd, &InitIndexData, m_indexBuffer.GetAddressOf());
        if (FAILED(hr))
        {
            qDebug() << "Hata: Index Buffer olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
            return false;
        }
    }
    else
    {
        m_indexBuffer.Reset();
        qDebug() << "Uyari: Mesh dosyasinda hic index yok: " << QString::fromStdWString(filePath);
    }

    qDebug() << "Mesh basariyla yuklendi: " << QString::fromStdWString(filePath);

    SetCameraToMeshBounds(); // Mesh yüklendikten sonra kamerayı otomatik olarak mesh'e göre ayarla

    return true;
}

//CN3VMesh* D3D11Renderer::GetCollisionMesh()
//{
//    return &m_collisionMesh;
//}

void D3D11Renderer::SetCamera(XMFLOAT3 pos, XMFLOAT3 target, XMFLOAT3 up)
{
    // Bu fonksiyon artık doğrudan kullanılmıyor, yerine SetupCamera() çağrılıyor
    // m_cameraPos = pos; // Bu satır artık doğrudan atanmıyor
    m_cameraTarget = target; // Hedef güncellenebilir
    m_cameraUp = up;         // Up vektörü güncellenebilir
    SetupCamera();
}

// Işın başlangıç noktası (world space)
DirectX::XMVECTOR D3D11Renderer::ScreenToWorldRayOrigin(float mouseX, float mouseY, int viewportWidth, int viewportHeight)
{
    // 1. Ekran koordinatlarını NDC'ye dönüştür (Normalized Device Coordinates)
    // D3D'de x = [-1, 1], y = [1, -1] (üst sol 0,0, alt sağ 1,1)
    float ndcX = (2.0f * mouseX / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / viewportHeight); // Y ekseni ters

    // 2. Işının yakın ve uzak düzlem noktalarını hesapla (NDC uzayında)
    DirectX::XMVECTOR nearPoint = DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f); // Z = 0 (yakın düzlem)
    DirectX::XMVECTOR farPoint = DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);  // Z = 1 (uzak düzlem)

    // 3. View ve Projection matrislerinin çarpımının tersini al
    DirectX::XMMATRIX viewProj = XMMatrixMultiply(m_viewMatrix, m_projectionMatrix);
    DirectX::XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

    // 4. Noktaları ters matrisle dönüştür (Dünya uzayına)
    DirectX::XMVECTOR worldNear = XMVector3TransformCoord(nearPoint, invViewProj);
    DirectX::XMVECTOR worldFar = XMVector3TransformCoord(farPoint, invViewProj);

    // Ray Origin (Kamera pozisyonu veya yakın düzlemdeki dünya noktası)
    // Genellikle ray origin doğrudan kamera pozisyonudur.
    // Ancak mouse pick için, yakın düzlemdeki noktayı kullanmak daha doğru sonuçlar verebilir.
    // Veya daha basit haliyle, kamera pozisyonunu kullanabilirsiniz.
    // Eğer kamera pozisyonu zaten m_cameraPos'ta tutuluyorsa:
    return XMLoadFloat3(&m_cameraPos); // Kamera pozisyonunu ray'in başlangıcı olarak kullan
    // Veya isterseniz worldNear'ı da kullanabilirsiniz, bu kameranın yakın düzlemindeki noktayı temsil eder:
    // return worldNear;
}

// Işın yönü (world space)
DirectX::XMVECTOR D3D11Renderer::ScreenToWorldRayDirection(float mouseX, float mouseY, int viewportWidth, int viewportHeight)
{
    // 1. Ekran koordinatlarını NDC'ye dönüştür
    float ndcX = (2.0f * mouseX / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / viewportHeight);

    // 2. Işının yakın ve uzak düzlem noktalarını hesapla (NDC uzayında)
    DirectX::XMVECTOR nearPoint = DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
    DirectX::XMVECTOR farPoint = DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

    // 3. View ve Projection matrislerinin çarpımının tersini al
    DirectX::XMMATRIX viewProj = XMMatrixMultiply(m_viewMatrix, m_projectionMatrix);
    DirectX::XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

    // 4. Noktaları ters matrisle dönüştür (Dünya uzayına)
    DirectX::XMVECTOR worldNear = XMVector3TransformCoord(nearPoint, invViewProj);
    DirectX::XMVECTOR worldFar = XMVector3TransformCoord(farPoint, invViewProj);

    // Ray Direction
    // worldFar - worldNear normalizasyonu bize ray yönünü verir.
    DirectX::XMVECTOR rayDirection = XMVector3Normalize(XMVectorSubtract(worldFar, worldNear));

    return rayDirection;
}

// *** BASİT NESNE SEÇİMİ (PICKING) FONKSİYONU ***
// Şimdilik sadece tek bir mesh'imiz (m_collisionMesh) olduğu varsayılıyor.
// Bu fonksiyon, ışının mesh'in sınırlayıcı kutusu (Bounding Box) ile kesişip kesişmediğini kontrol edebilir.
// CN3VMesh sınıfınızın içinde Bounding Box veya Bounding Sphere bilgisi varsa bu çok kolaylaşır.
// Eğer yoksa, bu kısım biraz daha detaylı olabilir (her üçgenle kesişim testi).

// D3D11Renderer.cpp içinde, PickMesh fonksiyonunuzu tamamen bununla değiştirin

// D3D11Renderer.cpp içinde, PickMesh fonksiyonunuzu tamamen bununla değiştirin

bool D3D11Renderer::PickMesh(DirectX::XMVECTOR rayOrigin, DirectX::XMVECTOR rayDirection)
{
    // Yüklenmiş bir mesh yoksa
    if (m_collisionMesh.VertexCount() == 0)
    {
        m_isMeshSelected = false;
        m_isDraggingMeshNow = false;
        return false;
    }

    DirectX::BoundingSphere meshBoundingSphere;

    DirectX::XMFLOAT3 tempMeshCenter = m_collisionMesh.GetCenter();
    DirectX::XMVECTOR meshLocalCenter = DirectX::XMLoadFloat3(&tempMeshCenter); // Şimdi tempMeshCenter'ın adresini alabiliriz.
    DirectX::XMVECTOR translatedCenter = DirectX::XMVectorAdd(meshLocalCenter, DirectX::XMLoadFloat3(&m_worldTranslation));

    // Sadece bu kısım KESİNLİKLE kalmalı. Diğer XMStoreFloat3 çağrısı silinmeli!
    meshBoundingSphere.Center.x = DirectX::XMVectorGetX(translatedCenter);
    meshBoundingSphere.Center.y = DirectX::XMVectorGetY(translatedCenter);
    meshBoundingSphere.Center.z = DirectX::XMVectorGetZ(translatedCenter);

    meshBoundingSphere.Radius = m_collisionMesh.GetRadius(); // CN3VMesh::GetRadius() çağrıldı

    float distance;
    if (meshBoundingSphere.Intersects(rayOrigin, rayDirection, distance))
    {
        m_isMeshSelected = true; // Mesh seçildi
        qDebug() << "Mesh Picked: True";
        return true;
    }
    else
    {
        m_isMeshSelected = false; // Hiçbir mesh seçilmedi veya başka bir yere tıklandı
        m_isDraggingMeshNow = false;
        qDebug() << "Mesh Picked: False";
        return false;
    }
}

void D3D11Renderer::CaptureSelectedMeshDepth(float mouseX, float mouseY, int widgetWidth, int widgetHeight)
{
    if (!m_isMeshSelected) return;

    DirectX::XMVECTOR rayOrigin = ScreenToWorldRayOrigin(mouseX, mouseY, widgetWidth, widgetHeight);
    DirectX::XMVECTOR rayDirection = ScreenToWorldRayDirection(mouseX, mouseY, widgetWidth, widgetHeight);

    DirectX::XMVECTOR meshWorldPos = DirectX::XMLoadFloat3(&m_worldTranslation);

    DirectX::XMVECTOR cameraPos = DirectX::XMLoadFloat3(&m_cameraPos); // <-- m_cameraPosition tanımlı olmalı
    DirectX::XMVECTOR toMesh = DirectX::XMVectorSubtract(meshWorldPos, cameraPos);

    float t = DirectX::XMVectorGetX(DirectX::XMVector3Dot(toMesh, rayDirection));

    DirectX::XMVECTOR clickedWorldPoint = DirectX::XMVectorAdd(rayOrigin, DirectX::XMVectorScale(rayDirection, t));
    DirectX::XMStoreFloat3(&m_previousMouseWorldPos, clickedWorldPoint);

    m_selectedMeshInitialDepth = t;

    qDebug() << "Initial Mesh Depth Captured (t value): " << m_selectedMeshInitialDepth;
    qDebug() << "Initial Mouse World Pos (for dragging delta): X=" << m_previousMouseWorldPos.x
        << " Y=" << m_previousMouseWorldPos.y
        << " Z=" << m_previousMouseWorldPos.z;
}

void D3D11Renderer::DragSelectedMesh(float currentMouseX, float currentMouseY, int widgetWidth, int widgetHeight)
{
    // DİKKAT: Burada m_isDraggingMeshNow kontrolü PickMesh'in başlatılıp başlatılmadığını kontrol etmek için önemlidir.
    // m_isMeshSelected kontrolü de obje seçili mi diye bakar.
    if (!m_isMeshSelected || !m_isDraggingMeshNow) return;

    DirectX::XMVECTOR currentRayOrigin = ScreenToWorldRayOrigin(currentMouseX, currentMouseY, widgetWidth, widgetHeight);
    DirectX::XMVECTOR currentRayDirection = ScreenToWorldRayDirection(currentMouseX, currentMouseY, widgetWidth, widgetHeight);

    DirectX::XMVECTOR newMouseWorldPos = DirectX::XMVectorAdd(currentRayOrigin, DirectX::XMVectorScale(currentRayDirection, m_selectedMeshInitialDepth));

    DirectX::XMVECTOR deltaWorldPos = DirectX::XMVectorSubtract(newMouseWorldPos, DirectX::XMLoadFloat3(&m_previousMouseWorldPos));

    DirectX::XMVECTOR currentMeshWorldTranslation = DirectX::XMLoadFloat3(&m_worldTranslation);
    currentMeshWorldTranslation = DirectX::XMVectorAdd(currentMeshWorldTranslation, deltaWorldPos);
    DirectX::XMStoreFloat3(&m_worldTranslation, currentMeshWorldTranslation);

    DirectX::XMStoreFloat3(&m_previousMouseWorldPos, newMouseWorldPos);

    qDebug() << "Mesh Dragged To: X=" << m_worldTranslation.x
        << " Y=" << m_worldTranslation.y
        << " Z=" << m_worldTranslation.z;
}

//void D3D11Renderer::MoveSelectedMesh(DirectX::XMVECTOR currentRayOrigin, DirectX::XMVECTOR currentRayDirection)
//{
//    if (!m_isMeshSelected) return; // Mesh seçili değilse hareket ettirme
//
//    // Objeyi seçtiğimiz anki derinliği (Z) koruyarak fareyi hareket ettirmek için bir düzlem kullanalım.
//    // Bu düzlem, objenin seçildiği andaki dünya pozisyonundan kameraya dik olmalı.
//    // Ya da daha basit bir yaklaşımla, objenin o anki Z derinliğini koruyarak fareyi XY düzleminde hareket ettirelim.
//    // Ancak sizin isteğiniz "her yöne" hareket ettirmek olduğu için, daha dinamik bir yaklaşım gerekli.
//
//    // En basit 6DOF (6 serbestlik derecesi) sürükleme için:
//    // Ray ile objenin başlangıç derinliğindeki bir düzlemle kesişimini bulalım.
//    // Bu, objeyi ekran düzlemi boyunca (XY) hareket ettirirken Z eksenini (derinliği) korumak için en yaygın yoldur.
//
//    // Objeyi seçtiğimiz andaki dünya pozisyonumuz (m_selectedMeshInitialWorldPos)
//    // ve başlangıç fare çarpışma noktamız (m_initialMouseWorldPos) elimizde.
//
//    // 1. Kameranın ileri (forward) vektörünü al (Bu, derinliği temsil eder).
//    // Kameranın dönüş matrisinden forward vektörü elde edebiliriz.
//    XMMATRIX viewMatrix = m_viewMatrix;
//    XMVECTOR cameraForward = XMVectorSet(viewMatrix.r[2].m128_f32[0], viewMatrix.r[2].m128_f32[1], viewMatrix.r[2].m128_f32[2], 0.0f);
//    // Bu forward vektör, kamera ile objenin o anki derinliği arasında bir düzlem tanımlamak için kullanılabilir.
//
//    // 2. Fare ışınının objenin başlangıç dünya pozisyonundan geçen düzlemle kesişimini bul.
//    // Bu düzlem objenin başlangıç dünya pozisyonundan geçiyor ve kameraya dik (veya kameranın baktığı yöne paralel)
//    // Eğer kamera hareket etmeyecekse ve objeyi ekrandaki fare pozisyonuna göre hareket ettireceksek,
//    // genellikle objenin başlangıç dünya Z koordinatını koruyan bir düzlem kullanırız.
//
//    // Daha sofistike bir yaklaşım için:
//    // Mesh'in o anki dünya pozisyonundan geçen ve kameraya bakan bir düzlem tanımlayalım.
//    // Bu, objeyi ekranda fare imleciyle birlikte hareket ettirme hissi verir.
//
//    // Düzlem normali: Kamera'dan objeye doğru (veya ray direction'ı).
//    // Şu anki rayOrigin kamera pozisyonudur.
//    XMVECTOR planeNormal = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&m_worldTranslation), currentRayOrigin));
//    if (XMVector3Equal(planeNormal, XMVectorZero())) // Kameradan objeye doğru bir vektör yoksa (çok yakınsa vb.)
//    {
//        // Basit bir varsayılan: Kameranın ileri yönü (eksenel olarak)
//        planeNormal = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // Varsayılan olarak Z ekseni (DirectX'te forward)
//    }
//
//    // Objeyi sürüklerken, objenin başlangıçtaki pozisyonuna göre bir düzlem oluşturun.
//    // Bu, objeyi XY düzleminde hareket ettirirken derinliğini korur.
//    // Normal, kameranın ileri vektörü olabilir veya daha basitçe dünya Z ekseni.
//    // En basit haliyle: XZ düzleminde hareket ederken Y'yi koru, XY düzleminde hareket ederken Z'yi koru.
//    // "Her yere sürüklemek" istiyorsanız, en mantıklı yol, fareyi obje üzerinde basılı tuttuğumuz andaki
//    // objenin dünya Z eksenindeki derinliğini koruyan bir düzlem kullanmaktır.
//
//    // Düzlemi, objenin seçildiği andaki dünya pozisyonundan geçecek şekilde oluşturun
//    // ve kameranın bakış yönüne paralel bir normal kullanın (veya objeye dik).
//    // Daha basit ve genellikle tercih edilen yöntem: Objeyi seçtiğimiz anki objenin derinliğini koruyarak hareket ettirmek.
//    // Yani objenin World Matrix'teki Z (veya hangi eksen derinliği temsil ediyorsa) koordinatını sabitlemek.
//
//    // Düzlem normali olarak kameranın ileri vektörünü alalım. Bu, objeyi ekran düzleminde sürüklememizi sağlar.
//    // Kamera pozisyonundan objeye doğru olan vektörü normal olarak kullanmak da mantıklıdır.
//    XMVECTOR cameraLookAt = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&m_worldTranslation), XMLoadFloat3(&m_cameraPos)));
//    XMVECTOR plane = XMPlaneFromPointNormal(XMLoadFloat3(&m_worldTranslation), cameraLookAt); // Objeden kameraya giden vektöre dik düzlem
//
//    // Fare ışınının bu düzlemle kesiştiği noktayı bulalım.
//    XMVECTOR intersectionPoint;
//    float t;
//    bool intersects = DirectX::TriangleTests::Intersects(currentRayOrigin, currentRayDirection, plane, t);
//
//    if (intersects)
//    {
//        intersectionPoint = currentRayOrigin + (currentRayDirection * t);
//
//        // Başlangıç fare çarpışma noktamız (m_initialMouseWorldPos) ile şu anki çarpışma noktası arasındaki farkı bul.
//        XMVECTOR delta = XMVectorSubtract(intersectionPoint, XMLoadFloat3(&m_initialMouseWorldPos));
//
//        // Bu delta değerini objenin dünya çevirisine ekle.
//        XMVECTOR currentMeshPos = XMLoadFloat3(&m_worldTranslation);
//        currentMeshPos = XMVectorAdd(currentMeshPos, delta);
//        XMStoreFloat3(&m_worldTranslation, currentMeshPos);
//
//        // Yeni başlangıç noktası olarak mevcut fare çarpışma noktasını kaydet, böylece delta sürekli doğru hesaplanır.
//        XMStoreFloat3(&m_initialMouseWorldPos, intersectionPoint);
//    }
//}

void D3D11Renderer::SetupCamera()
{
    // Kamera pozisyonunu hedef, radius, yaw ve pitch kullanarak hesapla
    XMVECTOR targetVec = XMLoadFloat3(&m_cameraTarget);

    // Yaw ve Pitch'i kullanarak bir rotasyon matrisi oluştur
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);

    // Kamerayı hedeften geriye doğru 'radius' kadar ötele
    XMVECTOR cameraOffset = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, -m_cameraRadius, 0.0f), rotationMatrix);

    // Kamera pozisyonu: Hedef + Offset
    XMVECTOR Eye = targetVec + cameraOffset;

    // m_cameraPos'u güncelle
    XMStoreFloat3(&m_cameraPos, Eye);

    XMVECTOR At = targetVec; // Hedef aynı kalır
    XMVECTOR Up = XMVectorSet(m_cameraUp.x, m_cameraUp.y, m_cameraUp.z, 0.0f); // Up vektörü aynı kalır

    m_viewMatrix = XMMatrixLookAtLH(Eye, At, Up);

    // Projeksiyon matrisi daha önce Resize içinde ayarlanmıştı, burada tekrar ayarlamaya gerek yok
    // m_projectionMatrix = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_width / (float)m_height, 0.01f, 1000.0f);
}

void D3D11Renderer::SetCameraToMeshBounds()
{
    // Mesh'in bounding box'ını al
    DirectX::XMFLOAT3 minBounds = m_collisionMesh.GetMinBounds();
    DirectX::XMFLOAT3 maxBounds = m_collisionMesh.GetMaxBounds();

    // Bounding box'ın merkezi
    m_cameraTarget.x = (minBounds.x + maxBounds.x) / 2.0f;
    m_cameraTarget.y = (minBounds.y + maxBounds.y) / 2.0f;
    m_cameraTarget.z = (minBounds.z + maxBounds.z) / 2.0f;

    // Bounding box boyutları
    float dx = maxBounds.x - minBounds.x;
    float dy = maxBounds.y - minBounds.y;
    float dz = maxBounds.z - minBounds.z;

    // Bounding box'ın merkezinden en uzak köşeye olan mesafe (bounding sphere radius)
    float boundingSphereRadius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);

    // Kameranın FOV'u (XM_PIDIV4 = 45 derece)
    float fovRadians = XM_PIDIV4;

    // Nesnenin tamamının görüş alanına sığması için gereken minimum mesafeyi hesapla
    // Biraz boşluk bırakmak için çarpan ekledim (örn: 1.5f)
    m_cameraRadius = boundingSphereRadius / tanf(fovRadians * 0.5f) * 1.5f;

    // Minimum bir radius belirle (çok küçük modeller için)
    m_cameraRadius = std::max(m_cameraRadius, 5.0f); // En az 5 birim uzakta başla

    // Maximum bir radius belirle (opsiyonel)
    m_cameraRadius = std::min(m_cameraRadius, 500.0f); // En fazla 500 birim uzakta

    // Kamerayı yeni pozisyona ayarla
    SetupCamera();
}

void D3D11Renderer::ZoomCamera(float deltaZ)
{
    // Fare tekerleği deltaZ değeri platforma göre değişebilir (genellikle 120 veya -120).
    // Daha pürüzsüz bir yakınlaştırma için m_zoomSpeed ile çarpıyoruz.
    // DirectX'te tekerlek yukarı (ileri) döndürüldüğünde pozitif delta (120) gelir.
    // Bu durumda radius'u azaltmak (yakınlaşmak) istiyoruz.
    float zoomAmount = deltaZ * m_zoomSpeed;
    m_cameraRadius -= zoomAmount;

    // Kameranın hedefe çok yaklaşmasını veya çok uzaklaşmasını engelle
    m_cameraRadius = std::max(0.1f, m_cameraRadius); // Minimum uzaklık 0.1 birim
    m_cameraRadius = std::min(1000.0f, m_cameraRadius); // Maksimum uzaklık 1000 birim

    SetupCamera(); // Kamera pozisyonunu yeniden hesapla
}

void D3D11Renderer::RotateCamera(float dx, float dy)
{
    m_yaw += dx * m_mouseSpeedX;
    m_pitch += dy * m_mouseSpeedY;

    // Pitch'i kısıtla (-PI/2 ile PI/2 arasında, doğrudan yukarı veya aşağı bakmayı engellemek için)
    m_pitch = std::max(-XM_PIDIV2 * 0.95f, m_pitch);
    m_pitch = std::min(XM_PIDIV2 * 0.95f, m_pitch);

    SetupCamera(); // Kamera pozisyonunu yeniden hesapla
}

// D3D11Renderer::PanCamera fonksiyonunun tanımı
void D3D11Renderer::PanCamera(float dx, float dy)
{
    // Pan hızı ve duyarlılığı ayarlanabilir.
    // dx, dy değerleri genellikle piksel cinsindendir, bu yüzden onları ölçeklemeliyiz.
    // Fare hızı ne kadar yüksek olursa, pan o kadar hassas olur (daha küçük çarpan).
    // Ekran boyutuna göre de ölçeklenebiliriz.
    float panSpeedX = 0.001f * m_cameraRadius; // Kameradan uzaklaştıkça daha hızlı pan
    float panSpeedY = 0.001f * m_cameraRadius; // Kameradan uzaklaştıkça daha hızlı pan

    XMVECTOR targetVec = XMLoadFloat3(&m_cameraTarget);
    XMVECTOR eyeVec = XMLoadFloat3(&m_cameraPos);
    XMVECTOR upVec = XMLoadFloat3(&m_cameraUp);

    // İleri yöne göre sağ vektörü hesapla (kameranın sağ tarafı)
    XMVECTOR forward = XMVector3Normalize(targetVec - eyeVec);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(upVec, forward));
    // Gerçek yukarı vektörü hesapla, böylece sağ ve ileri vektöre dik olur
    XMVECTOR actualUp = XMVector3Normalize(XMVector3Cross(forward, right));

    // Pan hareketini uygula
    // dx pozitifse sağa, negatifse sola hareket eder.
    // dy pozitifse aşağı, negatifse yukarı hareket eder (ekran Y ekseni genelde aşağı doğru artar).
    // Kamerayı yukarı kaydırmak için pozitif dy (ekranın aşağısı) ile çarpılır,
    // ya da kamera hedefi yukarı kaydırılırken fare yukarı hareket ettiğinde negatif dy kullanılır.
    // Blender'da fareyi yukarı hareket ettirmek ekranı yukarı kaydırır (kamerayı aşağıya doğru hareket ettirir).
    // Bu yüzden dy'yi negatif olarak kullanıyoruz.
    XMVECTOR panDelta = right * (-dx * panSpeedX) + actualUp * (dy * panSpeedY);

    // Kamera hedefini ve pozisyonunu güncelle
    XMStoreFloat3(&m_cameraTarget, targetVec + panDelta);
    XMStoreFloat3(&m_cameraPos, eyeVec + panDelta);

    // View matrisini yeniden hesapla
    SetupCamera();
}


void D3D11Renderer::SetWorldTranslation(float dx, float dy, float dz)
{
    m_worldTranslation.x += dx;
    m_worldTranslation.y += dy;
    m_worldTranslation.z += dz;
    // m_worldMatrix, Render() fonksiyonunda her karede yeniden oluşturulduğu için burada doğrudan atanmaya gerek yok.
    // Sadece m_worldTranslation'ı güncellemek yeterli.
}

void D3D11Renderer::SetCameraTargetY(float y)
{
    m_cameraTarget.y = y;
    SetupCamera(); // Hedef değiştiğinde kamerayı yeniden ayarla
}

float D3D11Renderer::GetCameraTargetY() const
{
    return m_cameraTarget.y;
}

void D3D11Renderer::SetWireframeMode(bool enable)
{
    m_wireframeMode = enable;
    // Render çağrısında RSSetState ile doğru rasterizer state'i ayarlanacak.
}

bool D3D11Renderer::CreateBuffers()
{
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(ConstantBufferData);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    HRESULT hr = m_d3dDevice->CreateBuffer(&bd, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Constant Buffer olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    // Vertex Input Layout Oluşturma
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // float3 Pos
        { "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 } // unsigned int Color -> float4 Color
    };
    UINT numElements = ARRAYSIZE(layout);

    hr = m_d3dDevice->CreateInputLayout(layout, numElements,
        m_vertexShaderBlob->GetBufferPointer(),
        m_vertexShaderBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Input Layout olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }
    return true;
}

bool D3D11Renderer::CreateRasterizerStates()
{
    HRESULT hr;

    D3D11_RASTERIZER_DESC rasterDesc = {}; // rasterDesc'i bir kez tanımla ve sıfırla

    // Solid Mode Rasterizer State
    D3D11_RASTERIZER_DESC solidDesc = {};
    solidDesc.FillMode = D3D11_FILL_SOLID;
    solidDesc.CullMode = D3D11_CULL_BACK;
    solidDesc.FrontCounterClockwise = FALSE;
    solidDesc.DepthClipEnable = TRUE;
    hr = m_d3dDevice->CreateRasterizerState(&solidDesc, m_solidRasterizerState.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Solid Rasterizer State olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    // Wireframe Mode Rasterizer State
    D3D11_RASTERIZER_DESC wireframeDesc = {};
    wireframeDesc.FillMode = D3D11_FILL_WIREFRAME;
    wireframeDesc.CullMode = D3D11_CULL_BACK;
    wireframeDesc.FrontCounterClockwise = FALSE;
    wireframeDesc.DepthClipEnable = TRUE;
    hr = m_d3dDevice->CreateRasterizerState(&wireframeDesc, m_wireframeRasterizerState.GetAddressOf());
    if (FAILED(hr)) {
        qDebug() << "Hata: Wireframe Rasterizer State olusturulurken hata! HRESULT: " << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    // --- Yeni: Cull Front (Ön Yüzleri Budayan) Rasterizer State (Dış Çizgi için) ---
    rasterDesc.FillMode = D3D11_FILL_SOLID; // Dış çizgi için solid olmalı
    rasterDesc.CullMode = D3D11_CULL_FRONT; // Ön yüzleri buda (arka yüzleri çiz)
    rasterDesc.DepthClipEnable = true;
    // İsteğe bağlı: Outline'ın her zaman görünmesi için DepthBias ekleyebilirsiniz
    // rasterDesc.DepthBias = 0;
    // rasterDesc.DepthBiasClamp = 0.0f;
    // rasterDesc.SlopeScaledDepthBias = 0.0f;
    hr = m_d3dDevice->CreateRasterizerState(&rasterDesc, m_cullFrontRasterizerState.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}