#include "Precompiled.h"
#include "window.h"

using namespace Microsoft::WRL;
using namespace D2D1;
using namespace std;

extern "C" IMAGE_DOS_HEADER __ImageBase;

static float const WindowWidth = 600.0f;
static float const WindowHeight = 400.0f;

struct ComException
{
    HRESULT result;

    ComException(HRESULT const value) : result(value)
    {
    }
};

static void HR(HRESULT const result)
{
    if (S_OK != result)
    {
        throw ComException(result);
    }
}

template <typename T> static float PhysicalToLogical(T const pixel, float const dpi)
{
    return pixel * 96.0f / dpi;
}

template <typename T> static float LogicalToPhysical(T const pixel, float const dpi)
{
    return pixel * dpi / 96.0f;
}

struct Circle
{
    ComPtr<IDCompositionVisual2> Visual;
    float LogicalX = 0.0f;
    float LogicalY = 0.0f;

    Circle(ComPtr<IDCompositionVisual2> &&visual, float const logicalX, float const logicalY, float const dpiX, float const dpiY) : Visual(move(visual))
    {
        SetLogicalOffset(logicalX, logicalY, dpiX, dpiY);
    }

    void SetLogicalOffset(float const logicalX, float const logicalY, float const dpiX, float const dpiY)
    {
        LogicalX = logicalX;
        LogicalY = logicalY;

        UpdateVisualOffset(dpiX, dpiY);
    }

    void UpdateVisualOffset(float const dpiX, float const dpiY)
    {
        HR(Visual->SetOffsetX(LogicalToPhysical(LogicalX, dpiX)));
        HR(Visual->SetOffsetY(LogicalToPhysical(LogicalY, dpiY)));
    }
};

struct SampleWindow : Window<SampleWindow>
{
    // Device independent resources
    float m_dpiX = 0.0f;
    float m_dpiY = 0.0f;
    ComPtr<ID2D1Factory2> m_factory;
    ComPtr<ID2D1EllipseGeometry> m_geometry;

    // Device resources
    ComPtr<ID3D11Device> m_device3D;
    ComPtr<IDCompositionDesktopDevice> m_device;
    ComPtr<IDCompositionTarget> m_target;
    ComPtr<IDCompositionVisual2> m_rootVisual;
    ComPtr<IDCompositionSurface> m_surface;
    list<Circle> m_circles;

    // Dragging a circle
    Circle *m_selected = nullptr;
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;

    SampleWindow()
    {
        CreateDesktopWindow();
        CreateFactoryAndGeometry();
    }

    void CreateFactoryAndGeometry()
    {
        D2D1_FACTORY_OPTIONS options = {};

#ifdef _DEBUG
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

        HR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, m_factory.GetAddressOf()));

        D2D1_ELLIPSE const ellipse = Ellipse(Point2F(50.0f, 50.0f), 49.0f, 49.0f);

        HR(m_factory->CreateEllipseGeometry(ellipse, m_geometry.GetAddressOf()));
    }

    void CreateDesktopWindow()
    {
        WNDCLASS wc = {};
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
        wc.lpszClassName = L"SampleWindow";
        wc.lpfnWndProc = WndProc;
        RegisterClass(&wc);

        ASSERT(!m_window);

        VERIFY(CreateWindowEx(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"Sample Window", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, this));

        ASSERT(m_window);
    }

    bool IsDeviceCreated() const
    {
        return m_device3D;
    }

    void ReleaseDeviceResources()
    {
        m_device3D.Reset();
    }

    void CreateDevice3D()
    {
        ASSERT(!IsDeviceCreated());

        unsigned flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;

#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, m_device3D.GetAddressOf(), nullptr, nullptr));
    }

    ComPtr<ID2D1Device> CreateDevice2D()
    {
        ComPtr<IDXGIDevice3> deviceX;
        HR(m_device3D.As(&deviceX));
        ComPtr<ID2D1Device> device2D;

        HR(m_factory->CreateDevice(deviceX.Get(), device2D.GetAddressOf()));

        return device2D;
    }

    ComPtr<IDCompositionVisual2> CreateVisual()
    {
        ComPtr<IDCompositionVisual2> visual;
        HR(m_device->CreateVisual(visual.GetAddressOf()));
        return visual;
    }

    void CreateDeviceResources()
    {
        ASSERT(!IsDeviceCreated());
        CreateDevice3D();
        ComPtr<ID2D1Device> const device2D = CreateDevice2D();

        HR(DCompositionCreateDevice2(device2D.Get(), __uuidof(m_device), reinterpret_cast<void **>(m_device.ReleaseAndGetAddressOf())));

        HR(m_device->CreateTargetForHwnd(m_window, true, m_target.ReleaseAndGetAddressOf()));

        m_rootVisual = CreateVisual();
        HR(m_target->SetRoot(m_rootVisual.Get()));
        CreateDeviceScaleResources();

        for (Circle &circle : m_circles)
        {
            circle.Visual = CreateVisual();
            HR(circle.Visual->SetContent(m_surface.Get()));
            HR(m_rootVisual->AddVisual(circle.Visual.Get(), false, nullptr));
            circle.UpdateVisualOffset(m_dpiX, m_dpiY);
        }

        HR(m_device->Commit());
    }

    void CreateDeviceScaleResources()
    {
        HR(m_device->CreateSurface(static_cast<unsigned>(LogicalToPhysical(100, m_dpiX)), static_cast<unsigned>(LogicalToPhysical(100, m_dpiY)), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, m_surface.ReleaseAndGetAddressOf()));

        ComPtr<ID2D1DeviceContext> dc;
        POINT offset = {};

        HR(m_surface->BeginDraw(nullptr, __uuidof(dc), reinterpret_cast<void **>(dc.GetAddressOf()), &offset));

        dc->SetDpi(m_dpiX, m_dpiY);

        dc->SetTransform(Matrix3x2F::Translation(PhysicalToLogical(offset.x, m_dpiX), PhysicalToLogical(offset.y, m_dpiY)));

        ComPtr<ID2D1SolidColorBrush> brush;
        D2D1_COLOR_F const color = ColorF(0.0f, 0.5f, 1.0f, 0.8f);

        HR(dc->CreateSolidColorBrush(color, brush.GetAddressOf()));

        dc->Clear();

        dc->FillGeometry(m_geometry.Get(), brush.Get());

        brush->SetColor(ColorF(1.0f, 1.0f, 1.0f));

        dc->DrawGeometry(m_geometry.Get(), brush.Get());

        HR(m_surface->EndDraw());
    }

    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
    {
        if (WM_MOUSEMOVE == message)
        {
            MouseMoveHandler(lparam);
        }
        else if (WM_LBUTTONDOWN == message)
        {
            MouseDownHandler(wparam, lparam);
        }
        else if (WM_LBUTTONUP == message)
        {
            MouseUpHandler();
        }
        else if (WM_PAINT == message)
        {
            PaintHandler();
        }
        else if (WM_DPICHANGED == message)
        {
            DpiChangedHandler(wparam, lparam);
        }
        else if (WM_CREATE == message)
        {
            CreateHandler();
        }
        else
        {
            return __super::MessageHandler(message, wparam, lparam);
        }

        return 0;
    }

    void MouseUpHandler()
    {
        ReleaseCapture();
        m_selected = nullptr;
    }

    void MouseDownHandler(WPARAM const wparam, LPARAM const lparam)
    {
        try
        {
            if (wparam & MK_CONTROL)
            {
                ComPtr<IDCompositionVisual2> visual = CreateVisual();
                HR(visual->SetContent(m_surface.Get()));
                HR(m_rootVisual->AddVisual(visual.Get(), false, nullptr));

                m_circles.emplace_front(move(visual), PhysicalToLogical(LOWORD(lparam), m_dpiX) - 50.0f, PhysicalToLogical(HIWORD(lparam), m_dpiY) - 50.0f, m_dpiX, m_dpiY);

                SetCapture(m_window);
                m_selected = &m_circles.front();
                m_mouseX = 50.0f;
                m_mouseY = 50.0f;
            }
            else
            {
                D2D1_MATRIX_3X2_F const transform = Matrix3x2F::Scale(m_dpiX / 96.0f, m_dpiY / 96.0f);

                for (auto circle = begin(m_circles); circle != end(m_circles); ++circle)
                {
                    D2D1_POINT_2F const point = Point2F(LOWORD(lparam) - LogicalToPhysical(circle->LogicalX, m_dpiX), HIWORD(lparam) - LogicalToPhysical(circle->LogicalY, m_dpiY));

                    BOOL contains = false;

                    HR(m_geometry->FillContainsPoint(point, transform, &contains));

                    if (contains)
                    {
                        HR(m_rootVisual->RemoveVisual(circle->Visual.Get()));
                        HR(m_rootVisual->AddVisual(circle->Visual.Get(), false, nullptr));
                        m_circles.splice(begin(m_circles), m_circles, circle);

                        SetCapture(m_window);
                        m_selected = &*circle;
                        m_mouseX = PhysicalToLogical(point.x, m_dpiX);
                        m_mouseY = PhysicalToLogical(point.y, m_dpiY);
                        break;
                    }
                }
            }

            HR(m_device->Commit());
        }
        catch (ComException const &e)
        {
            TRACE(L"MouseDownHandler failed 0x%X\n", e.result);

            ReleaseDeviceResources();

            VERIFY(InvalidateRect(m_window, nullptr, false));
        }
    }

    void MouseMoveHandler(LPARAM const lparam)
    {
        try
        {
            if (!m_selected)
                return;

            m_selected->SetLogicalOffset(PhysicalToLogical(GET_X_LPARAM(lparam), m_dpiX) - m_mouseX, PhysicalToLogical(GET_Y_LPARAM(lparam), m_dpiY) - m_mouseY, m_dpiX, m_dpiY);

            HR(m_device->Commit());
        }
        catch (ComException const &e)
        {
            TRACE(L"MouseMoveHandler failed 0x%X\n", e.result);

            ReleaseDeviceResources();

            VERIFY(InvalidateRect(m_window, nullptr, false));
        }
    }

    void DpiChangedHandler(WPARAM const wparam, LPARAM const lparam)
    {
        try
        {
            float const prevDpiX = m_dpiX;
            float const prevDpiY = m_dpiY;

            m_dpiX = LOWORD(wparam);
            m_dpiY = HIWORD(wparam);

            RECT rect = {};
            VERIFY(GetClientRect(m_window, &rect));

            rect.right = static_cast<long>(LogicalToPhysical(PhysicalToLogical(rect.right, prevDpiX), m_dpiX));
            rect.bottom = static_cast<long>(LogicalToPhysical(PhysicalToLogical(rect.bottom, prevDpiY), m_dpiY));

            VERIFY(AdjustWindowRect(&rect, GetWindowLong(m_window, GWL_STYLE), false));

            RECT const *position = reinterpret_cast<RECT const *>(lparam);

            VERIFY(SetWindowPos(m_window, nullptr, position->left, position->top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOZORDER));

            if (!IsDeviceCreated())
                return;

            CreateDeviceScaleResources();

            for (Circle &circle : m_circles)
            {
                HR(circle.Visual->SetContent(m_surface.Get()));
                circle.UpdateVisualOffset(m_dpiX, m_dpiY);
            }

            HR(m_device->Commit());
        }
        catch (ComException const &e)
        {
            TRACE(L"DpiChangedHandler failed 0x%X\n", e.result);

            ReleaseDeviceResources();

            VERIFY(InvalidateRect(m_window, nullptr, false));
        }
    }

    void CreateHandler()
    {
        HMONITOR const monitor = MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST);

        unsigned dpiX = 0;
        unsigned dpiY = 0;

        HR(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY));

        m_dpiX = static_cast<float>(dpiX);
        m_dpiY = static_cast<float>(dpiY);

        RECT rect = {0, 0, static_cast<LONG>(LogicalToPhysical(WindowWidth, m_dpiX)), static_cast<LONG>(LogicalToPhysical(WindowHeight, m_dpiY))};

        VERIFY(AdjustWindowRect(&rect, GetWindowLong(m_window, GWL_STYLE), false));

        VERIFY(SetWindowPos(m_window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER));
    }

    void PaintHandler()
    {
        try
        {
            if (IsDeviceCreated())
            {
                HR(m_device3D->GetDeviceRemovedReason());
            }
            else
            {
                CreateDeviceResources();
            }

            VERIFY(ValidateRect(m_window, nullptr));
        }
        catch (ComException const &e)
        {
            TRACE(L"PaintHandler failed 0x%X\n", e.result);

            ReleaseDeviceResources();
        }
    }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    SampleWindow window;
    MSG message;

    while (GetMessage(&message, nullptr, 0, 0))
    {
        DispatchMessage(&message);
    }
}
