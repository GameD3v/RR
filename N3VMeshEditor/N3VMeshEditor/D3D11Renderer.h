// D3D11Renderer.h
#pragma once

#include <d3d11.h>
#include <wrl/client.h> // Microsoft::WRL::ComPtr için
#include <DirectXMath.h> // DirectX Math kütüphanesi için
#include <DirectXCollision.h>
#include <vector>        // std::vector için
#include "CN3VMesh.h" // CN3VMesh sınıfını dahil et

// Shader'a gönderilecek sabit tampon yapısı
struct ConstantBufferData
{
    DirectX::XMMATRIX World;
    DirectX::XMMATRIX View;
    DirectX::XMMATRIX Projection;
    int RenderMode; // 0: normal, 1: secili (iç renk), 2: dış çizgi
};

// Izgara çizimi için Vertex yapısı
struct GridVertex {
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT4 Color;
};

class D3D11Renderer
{
public:
    D3D11Renderer();
    ~D3D11Renderer();

    bool Initialize(HWND hWnd, int width, int height);
    void Shutdown();
    void Render();
    void Resize(int width, int height);

    bool LoadMesh(const std::wstring& filePath); // Dosya yolu std::wstring olarak alacak
    CN3VMesh* GetCollisionMesh() { return &m_collisionMesh; } // Eklenen GetCollisionMesh fonksiyonu

    // Kamera kontrol fonksiyonları
    void SetCamera(DirectX::XMFLOAT3 pos, DirectX::XMFLOAT3 target, DirectX::XMFLOAT3 up); // Bu fonksiyon artık kullanılmıyor, SetupCamera çağrılıyor
    void SetupCamera(); // Kamera pozisyonunu hedef, radius, yaw ve pitch kullanarak hesaplar
    void SetCameraToMeshBounds(); // Mesh yüklendikten sonra kamerayı mesh'in boyutlarına göre ayarlar
    void ZoomCamera(float deltaZ);
    void RotateCamera(float dx, float dy);
    void PanCamera(float dx, float dy); // <<--- YENİ EKLENEN FONKSİYON BİLDİRİMİ
    void SetWorldTranslation(float dx, float dy, float dz);

    void SetCameraTargetY(float y);
    float GetCameraTargetY() const; // Kamera hedefinin Y bileşenini döndürmek için
    void SetWireframeMode(bool enable); // Kafes modu açma/kapama

    // Ekran koordinatlarından 3D ışın başlangıç noktası ve yönünü hesaplar
    // Bu fonksiyonlar, QDirect3D11Widget'tan çağrılacak.
    DirectX::XMVECTOR ScreenToWorldRayOrigin(float mouseX, float mouseY, int width, int height);
    DirectX::XMVECTOR ScreenToWorldRayDirection(float mouseX, float mouseY, int width, int height);

    // Mesh seçme fonksiyonu: bool PickMesh(rayOrigin, rayDirection) yerine
    // şimdi void PickMesh() kullanacağız ve m_isMeshSelected'ı içeride yöneteceğiz.
    // Eğer sahnedeki objeye tıklanırsa m_isMeshSelected = true olur.
    // Boş alana tıklanırsa m_isMeshSelected = false olur.
    bool PickMesh(DirectX::XMVECTOR rayOrigin, DirectX::XMVECTOR rayDirection);

    // Yeni: Seçili objenin başlangıçtaki derinliğini yakalamak için
    // Bu, mesh'in seçildiği anki fare pozisyonunun dünya koordinatındaki Z derinliğini depolar.
    void CaptureSelectedMeshDepth(float mouseX, float mouseY, int widgetWidth, int widgetHeight);

    // Yeni: Objeyi ekranda sürükleme fonksiyonu
    // Bu fonksiyon, fare pozisyonunu alacak ve objenin yeni dünya pozisyonunu hesaplayacak.
    void DragSelectedMesh(float currentMouseX, float currentMouseY, int widgetWidth, int widgetHeight);

    // Seçili mesh'i hareket ettirme fonksiyonu
    void MoveSelectedMesh(DirectX::XMVECTOR currentRayOrigin, DirectX::XMVECTOR currentRayDirection);

    // DİKKAT: m_isDraggingMeshNow ve m_isMeshSelected private olduğu için
    // QDirect3D11Widget'ın bunlara doğrudan erişebilmesi için SETTER fonksiyonları da eklememiz gerekiyor.
    // Ancak, direkt set etmek yerine PickMesh içinde yönetmek daha güvenli.
    // Eğer PickMesh içindeki mantık yeterli değilse, bu set fonksiyonlarını ekleriz.
    // Şu anki PickMesh ve mousePressEvent mantığı ile doğrudan set etmeye gerek kalmaması gerekiyor.
    // Yalnızca m_isDraggingMeshNow'ı mousePressEvent içinde true/false yapmamız gerekecek.
    // Bunun için de PickMesh'e değil, direkt D3D11Renderer'a public setter eklememiz lazım.
    bool IsMeshSelected() const { return m_isMeshSelected; }
    bool IsDraggingMeshNow() const { return m_isDraggingMeshNow; }
    void SetIsDraggingMeshNow(bool isDragging) { m_isDraggingMeshNow = isDragging; } // <<-- Bu setter çok önemli
    void SetIsMeshSelected(bool isSelected) { m_isMeshSelected = isSelected; }       // <<-- Bu setter da önemli

private:
    // Izgara çizim fonksiyonları
    void DrawGrid(); // Izgarayı çizmek için yeni fonksiyon
    void CreateGridBuffers(float size, int subdivisions); // Izgara Vertex Buffer'ını oluşturmak için

private:
    // DirectX 11 Cihaz ve Bağlam
    Microsoft::WRL::ComPtr<ID3D11Device>            m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>     m_d3dContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain>          m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_renderTargetView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_depthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_depthStencilView;

    // Shaderlar ve Input Layout
    Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer>            m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3DBlob>                m_vertexShaderBlob; // InputLayout oluşturmak için gerekli

    // Kamera ve dünya matrisleri
    DirectX::XMMATRIX m_worldMatrix;
    DirectX::XMMATRIX m_viewMatrix;
    DirectX::XMMATRIX m_projectionMatrix;

    CN3VMesh m_collisionMesh; // Yüklenen mesh verilerini tutar

    // Kamera kontrol değişkenleri
    DirectX::XMFLOAT3 m_cameraPos;
    DirectX::XMFLOAT3 m_cameraTarget;
    DirectX::XMFLOAT3 m_cameraUp;
    float m_cameraRadius;
    float m_yaw;
    float m_pitch;

    float m_zoomSpeed;
    float m_mouseSpeedX;
    float m_mouseSpeedY;

    DirectX::XMFLOAT3 m_worldTranslation = { 0.0f, 0.0f, 0.0f }; // Mesh'in dünya üzerindeki çeviri vektörü

    // Rasterizer State'leri
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_solidRasterizerState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_wireframeRasterizerState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_cullFrontRasterizerState;
    bool m_wireframeMode; // Kafes modu aktif mi?

    // Seçilen mesh'i takip etmek için
    // Eğer sahnenizde birden fazla mesh varsa, bu bir pointer veya index olabilir.
    // Şimdilik sadece m_collisionMesh'imiz olduğu için doğrudan onu manipüle edebiliriz.
    // Ancak seçili olduğunu belirtmek için bir bayrak tutalım:
    bool m_isMeshSelected = false; // m_collisionMesh'in seçili olup olmadığını belirtir.

    // Mesh sürükleme için önceki fare pozisyonunu ve derinliğini sakla
    DirectX::XMFLOAT3 m_selectedMeshInitialWorldPos; // Mesh seçildiği anki dünya pozisyonu
    DirectX::XMFLOAT3 m_initialMouseWorldPos;        // Mesh seçildiği anki fare ray'inin dünya pozisyonu

    // Yardımcı fonksiyonlar
    bool CompileShader(const char* shaderCode, const char* entryPoint, const char* profile, ID3DBlob** blob);
    bool CreateBuffers();
    bool CreateRasterizerStates(); // Yeni: Rasterizer State'leri oluşturmak için

    // Izgara ile ilgili üye değişkenleri
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_gridVertexBuffer; // Izgara için Vertex Buffer
    int m_gridVertexCount; // Izgaradaki toplam köşe sayısı

    // Mesh verileri için Buffer'lar
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;

    int m_width;
    int m_height;

    DirectX::XMFLOAT3 m_previousMouseWorldPos; // Mesh'i sürüklerken önceki dünya koordinatındaki fare pozisyonu
    float m_selectedMeshInitialDepth; // Mesh seçildiği anki derinliği (projeksiyon düzleminde)
    // Bu bayrak, objeyi sürükleme modunda olduğumuzu belirtir.
    // m_isMeshSelected'dan farklı olarak, sadece fare basılıyken sürükleme yapıldığında true olacak.
    bool m_isDraggingMeshNow = false;

    bool m_isOrbiting; // Şimdilik kullanılmıyor, mouseMoveEvent içinde direkt kontrol ediyoruz
    // QPoint m_lastMousePos; // Artık QDirect3D11Widget'ta tutuluyor
};