#include <OverlayRenderHandlerD3D11.h>
#include <OverlayClient.h>

#include <SpriteBatch.h>
#include <DirectXColors.h>
#include <SimpleMath.h>
#include <CommonStates.h>
#include <WICTextureLoader.h>


OverlayRenderHandlerD3D11::OverlayRenderHandlerD3D11(Renderer* apRenderer) noexcept
    : m_pRenderer(apRenderer)
{
}

OverlayRenderHandlerD3D11::~OverlayRenderHandlerD3D11() = default;

void OverlayRenderHandlerD3D11::Render()
{
    // First of all we flush our deferred context in case we have updated the texture
    {
        std::unique_lock<std::mutex> _(m_textureLock);

        Microsoft::WRL::ComPtr<ID3D11CommandList> pCommandList;
        const auto result = m_pContext->FinishCommandList(FALSE, &pCommandList);

        if (result == S_OK && pCommandList)
        {
            m_pImmediateContext->ExecuteCommandList(pCommandList.Get(), TRUE);
        }
    }

    GetRenderTargetSize();

    if (IsVisible())
    {
        m_pSpriteBatch->Begin(DirectX::SpriteSortMode_Deferred, m_pStates->NonPremultiplied());

        {
            std::unique_lock<std::mutex> _(m_textureLock);

            if (m_pTextureView)
                m_pSpriteBatch->Draw(m_pTextureView.Get(), DirectX::SimpleMath::Vector2(0.f, 0.f), nullptr, DirectX::Colors::White, 0.f);
        }
        
        if(m_pCursorTexture)
            m_pSpriteBatch->Draw(m_pCursorTexture.Get(), DirectX::SimpleMath::Vector2(100, 100), nullptr, DirectX::Colors::White, 0.f, DirectX::SimpleMath::Vector2(m_cursorX, m_cursorY), m_width / 1920.f);
        
        m_pSpriteBatch->End();
    }
}

void OverlayRenderHandlerD3D11::Reset()
{
    Create();
}

void OverlayRenderHandlerD3D11::Create()
{
    const auto hr = m_pRenderer->GetSwapChain()->GetDevice(IID_ID3D11Device, reinterpret_cast<void**>(m_pDevice.ReleaseAndGetAddressOf()));

    if (FAILED(hr))
        return;

    m_pDevice->GetImmediateContext(m_pImmediateContext.ReleaseAndGetAddressOf());

    if (!m_pContext)
        return;

    if (FAILED(m_pDevice->CreateDeferredContext(0, m_pContext.ReleaseAndGetAddressOf())))
        return;

    m_pSpriteBatch = std::make_unique<DirectX::SpriteBatch>(m_pContext.Get());
    m_pStates = std::make_unique<DirectX::CommonStates>(m_pDevice.Get());

    DirectX::CreateWICTextureFromFile(m_pDevice.Get(), L"Data\\Online\\UI\\assets\\images\\cursor.png", nullptr, m_pCursorTexture.ReleaseAndGetAddressOf());

    GetRenderTargetSize();
    CreateRenderTexture();
}

void OverlayRenderHandlerD3D11::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect)
{
    rect = CefRect(0, 0, m_width, m_height);
}

void OverlayRenderHandlerD3D11::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
    const RectList& dirtyRects, const void* buffer, int width, int height)
{
    if (type == PET_VIEW)
    {
        if (!m_pTexture)
        {
            CreateRenderTexture();
        }

        std::unique_lock<std::mutex> _(m_textureLock);

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        m_pContext->Map(m_pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        const auto pDest = static_cast<uint8_t*>(mappedResource.pData);
        std::memcpy(pDest, buffer, width * height * 4);
        m_pContext->Unmap(m_pTexture.Get(), 0);
    }
}

void OverlayRenderHandlerD3D11::GetRenderTargetSize()
{
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pRenderTargetView;

    m_pImmediateContext->OMGetRenderTargets(1, pRenderTargetView.ReleaseAndGetAddressOf(), nullptr);
    if (pRenderTargetView)
    {
        Microsoft::WRL::ComPtr<ID3D11Resource> pSrcResource;
        pRenderTargetView->GetResource(pSrcResource.ReleaseAndGetAddressOf());

        if (pSrcResource)
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> pSrcBuffer;
            pSrcResource.As(&pSrcBuffer);

            D3D11_TEXTURE2D_DESC desc;
            pSrcBuffer->GetDesc(&desc);

            if ((m_width != desc.Width || m_height != desc.Height) && m_pParent)
            {
                m_width = desc.Width;
                m_height = desc.Height;

                {
                    std::unique_lock<std::mutex> _(m_textureLock);

                    m_pTexture.Reset();
                    m_pTextureView.Reset();
                }

                m_pParent->GetBrowser()->GetHost()->WasResized();
            }
        }
    }
}

void OverlayRenderHandlerD3D11::CreateRenderTexture()
{
    std::unique_lock<std::mutex> _(m_textureLock);

    D3D11_TEXTURE2D_DESC textDesc;
    textDesc.Width = m_width;
    textDesc.Height = m_height;
    textDesc.MipLevels = textDesc.ArraySize = 1;
    textDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textDesc.SampleDesc.Count = 1;
    textDesc.SampleDesc.Quality = 0;
    textDesc.Usage = D3D11_USAGE_DYNAMIC;
    textDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    textDesc.MiscFlags = 0;

    if (FAILED(m_pDevice->CreateTexture2D(&textDesc, nullptr, m_pTexture.ReleaseAndGetAddressOf())))
        return;

    D3D11_SHADER_RESOURCE_VIEW_DESC sharedResourceViewDesc = {};
    sharedResourceViewDesc.Format = textDesc.Format;
    sharedResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sharedResourceViewDesc.Texture2D.MipLevels = 1;

    if (FAILED(m_pDevice->CreateShaderResourceView(m_pTexture.Get(), &sharedResourceViewDesc, m_pTextureView.ReleaseAndGetAddressOf())))
        return;
}
