#include "app/Profile.h"
#include "app/Font5x7.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <windows.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "comdlg32.lib")

namespace ludora {

namespace {

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/// Escapa saltos de linea para que el archivo siga siendo clave=valor.
std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            if      (n == 'n')  out += '\n';
            else if (n == 'r')  out += '\r';
            else if (n == '\\') out += '\\';
            else                out += n;
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace

std::string Profile::clampChars(const std::string& utf8, size_t maxChars) {
    // Recortar por caracteres: cortar a mitad de un acento dejaria una
    // secuencia UTF-8 invalida en disco.
    const char* p = utf8.c_str();
    const char* start = p;
    for (size_t i = 0; i < maxChars && *p; ++i) font5x7::nextUtf8(p);
    return utf8.substr(0, static_cast<size_t>(p - start));
}

bool Profile::load(const std::wstring& path, Profile& out) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = unescape(trim(line.substr(eq + 1)));
        if      (k == "user")  out.userName    = v;
        else if (k == "desc")  out.description = v;
        else if (k == "photo") out.photoFile   = v;
    }
    return true;
}

bool Profile::save(const std::wstring& path, const Profile& p) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;

    out << "# Perfil de Ludora Engine (UTF-8).\r\n"
        << "user="  << escape(clampChars(p.userName, kMaxUserName))       << "\r\n"
        << "desc="  << escape(clampChars(p.description, kMaxDescription)) << "\r\n"
        << "photo=" << escape(p.photoFile)                                << "\r\n";
    return out.good();
}

// ---------------------------------------------------------------- foto ----
namespace photo {

std::wstring pickFromGallery(void* ownerHwnd) {
    wchar_t file[MAX_PATH]{};

    OPENFILENAMEW ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = static_cast<HWND>(ownerHwnd);
    ofn.lpstrFilter  =
        L"Imagenes\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.heic\0"
        L"Todos los archivos\0*.*\0";
    ofn.lpstrFile    = file;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = L"Elegir foto de perfil";
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return {};
    return file;
}

bool loadBmp(const std::wstring& fileName, std::vector<u32>& pixels, i32& w, i32& h) {
    std::ifstream in(fileName.c_str(), std::ios::binary);
    if (!in) return false;

    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    in.read(reinterpret_cast<char*>(&fh), sizeof(fh));
    in.read(reinterpret_cast<char*>(&ih), sizeof(ih));
    if (!in || fh.bfType != 0x4D42) return false;
    if (ih.biBitCount != 32 && ih.biBitCount != 24) return false;
    if (ih.biCompression != BI_RGB) return false;

    w = ih.biWidth;
    h = std::abs(ih.biHeight);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;

    const bool topDown = ih.biHeight < 0;
    const i32  bpp     = ih.biBitCount / 8;
    // Cada fila del BMP se alinea a 4 bytes.
    const size_t stride = ((static_cast<size_t>(w) * bpp + 3) / 4) * 4;

    std::vector<u8> row(stride);
    pixels.assign(static_cast<size_t>(w) * h, 0xFF000000u);

    in.seekg(fh.bfOffBits, std::ios::beg);
    for (i32 y = 0; y < h; ++y) {
        in.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(stride));
        if (!in) return false;
        const i32 dstY = topDown ? y : (h - 1 - y);
        for (i32 x = 0; x < w; ++x) {
            const u8* px = row.data() + static_cast<size_t>(x) * bpp;
            const u32 b = px[0], g = px[1], r = px[2];
            pixels[static_cast<size_t>(dstY) * w + x] =
                0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    return true;
}

/// Guarda pixeles BGRA como BMP de 32 bits top-down.
static bool saveBmp32(const std::wstring& dest, const std::vector<u32>& px, i32 w, i32 h) {
    std::ofstream out(dest.c_str(), std::ios::binary);
    if (!out) return false;

    const u32 bytes = static_cast<u32>(w) * h * 4;
    BITMAPFILEHEADER fh{};
    fh.bfType    = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize    = fh.bfOffBits + bytes;

    BITMAPINFOHEADER ih{};
    ih.biSize        = sizeof(ih);
    ih.biWidth       = w;
    ih.biHeight      = -h;          // top-down
    ih.biPlanes      = 1;
    ih.biBitCount    = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage   = bytes;

    out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
    out.write(reinterpret_cast<const char*>(&ih), sizeof(ih));
    out.write(reinterpret_cast<const char*>(px.data()), bytes);
    return out.good();
}

bool loadAnyImage(const std::wstring& file, std::vector<u32>& pixels, i32& w, i32& h) {
    // WIC cubre PNG, JPEG, GIF, TIFF, BMP, ICO, HEIF y WebP segun los codecs
    // instalados: asi no hay que escribir un decodificador por formato.
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool weInit = SUCCEEDED(hrInit);
    // RPC_E_CHANGED_MODE: el hilo ya estaba en otro modo; se puede seguir.
    if (!weInit && hrInit != RPC_E_CHANGED_MODE) return false;

    bool ok = false;
    IWICImagingFactory* factory = nullptr;

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        IWICBitmapDecoder* dec = nullptr;
        if (SUCCEEDED(factory->CreateDecoderFromFilename(
                file.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &dec))) {
            IWICBitmapFrameDecode* frame = nullptr;
            if (SUCCEEDED(dec->GetFrame(0, &frame))) {
                IWICFormatConverter* conv = nullptr;
                if (SUCCEEDED(factory->CreateFormatConverter(&conv))) {
                    // Normalizar a BGRA de 32 bits, el formato del framebuffer.
                    if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                                   WICBitmapPaletteTypeCustom))) {
                        UINT uw = 0, uh = 0;
                        if (SUCCEEDED(conv->GetSize(&uw, &uh)) &&
                            uw > 0 && uh > 0 && uw <= 16384 && uh <= 16384) {
                            w = static_cast<i32>(uw);
                            h = static_cast<i32>(uh);
                            pixels.assign(static_cast<size_t>(w) * h, 0);
                            const UINT stride = uw * 4;
                            const UINT total  = stride * uh;
                            if (SUCCEEDED(conv->CopyPixels(
                                    nullptr, stride, total,
                                    reinterpret_cast<BYTE*>(pixels.data())))) {
                                ok = true;
                            }
                        }
                    }
                    conv->Release();
                }
                frame->Release();
            }
            dec->Release();
        }
        factory->Release();
    }

    if (weInit) CoUninitialize();
    return ok;
}

ContentCheck checkImageContent(const std::vector<u32>& pixels, i32 w, i32 h) {
    ContentCheck out;
    if (pixels.empty() || w <= 0 || h <= 0) return out;

    // Umbrales. Son deliberadamente permisivos: ante la duda se acepta y se
    // deja la decision a una revision humana, porque un falso positivo aqui
    // bloquea a una persona por una foto legitima.
    constexpr f32 kSkinReject   = 0.62f;   // piel en casi todo el encuadre
    constexpr f32 kCenterReject = 0.75f;   // y concentrada en el centro

    size_t skin = 0, total = 0, centerSkin = 0, centerTotal = 0;

    const i32 cx0 = w / 4, cx1 = w - w / 4;
    const i32 cy0 = h / 4, cy1 = h - h / 4;

    // Muestreo: en imagenes grandes no hace falta mirar cada pixel.
    const i32 step = std::max(1, std::min(w, h) / 256);

    for (i32 y = 0; y < h; y += step) {
        for (i32 x = 0; x < w; x += step) {
            const u32 p = pixels[static_cast<size_t>(y) * w + x];
            const i32 r = static_cast<i32>((p >> 16) & 0xFF);
            const i32 g = static_cast<i32>((p >> 8)  & 0xFF);
            const i32 b = static_cast<i32>( p        & 0xFF);

            // Regla RGB clasica de deteccion de tono de piel (Kovac et al.).
            // Cubre un rango amplio de tonos, de claro a oscuro.
            const i32 mx = std::max(r, std::max(g, b));
            const i32 mn = std::min(r, std::min(g, b));
            const bool esPiel =
                r > 95 && g > 40 && b > 20 &&
                (mx - mn) > 15 &&
                std::abs(r - g) > 15 &&
                r > g && r > b;

            ++total;
            if (esPiel) ++skin;

            if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) {
                ++centerTotal;
                if (esPiel) ++centerSkin;
            }
        }
    }

    if (total == 0) return out;

    out.skinRatio = static_cast<f32>(skin) / static_cast<f32>(total);
    const f32 centerRatio = centerTotal
        ? static_cast<f32>(centerSkin) / static_cast<f32>(centerTotal)
        : 0.0f;

    // Se exigen AMBAS condiciones: mucha piel en total Y concentrada en el
    // centro. Solo una de las dos marcaria retratos y primeros planos.
    if (out.skinRatio >= kSkinReject && centerRatio >= kCenterReject) {
        out.allowed = false;
        out.reason  = "La imagen parece contener desnudez. Si es un error, "
                      "elige otra foto o solicita revision.";
    }
    return out;
}

bool importImage(const std::wstring& src, const std::wstring& dest,
                 std::string& error, ContentCheck* check) {
    std::vector<u32> px;
    i32 w = 0, h = 0;

    // Cualquier formato que Windows sepa decodificar; BMP como respaldo por
    // si WIC no esta disponible.
    if (!loadAnyImage(src, px, w, h) && !loadBmp(src, px, w, h)) {
        error = "No se pudo leer la imagen (formato no reconocido).";
        return false;
    }

    const ContentCheck cc = checkImageContent(px, w, h);
    if (check) *check = cc;
    if (!cc.allowed) {
        // Se explica el motivo: rechazar en silencio dejaria al usuario sin
        // saber que ha pasado ni como resolverlo.
        error = cc.reason;
        return false;
    }

    if (!saveBmp32(dest, px, w, h)) {
        error = "No se pudo guardar la foto.";
        return false;
    }
    return true;
}

namespace {

/// Envuelve la inicializacion de Media Foundation para no dejarla arrancada.
struct MfSession {
    bool ok = false;
    MfSession()  { ok = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET)); }
    ~MfSession() { if (ok) MFShutdown(); }
};

/// Primera camara de video del sistema. El llamante libera el resultado.
IMFActivate* firstCamera() {
    IMFAttributes* attrs = nullptr;
    if (FAILED(MFCreateAttributes(&attrs, 1))) return nullptr;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    const HRESULT hr = MFEnumDeviceSources(attrs, &devices, &count);
    attrs->Release();
    if (FAILED(hr) || count == 0) {
        if (devices) CoTaskMemFree(devices);
        return nullptr;
    }

    IMFActivate* first = devices[0];
    first->AddRef();
    for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
    CoTaskMemFree(devices);
    return first;
}

} // namespace

bool cameraAvailable() {
    MfSession mf;
    if (!mf.ok) return false;
    IMFActivate* cam = firstCamera();
    const bool hay = (cam != nullptr);
    if (cam) cam->Release();
    return hay;
}

bool captureFromCamera(const std::wstring& dest, std::string& error) {
    MfSession mf;
    if (!mf.ok) { error = "No se pudo iniciar el subsistema de video."; return false; }

    IMFActivate* cam = firstCamera();
    if (!cam) { error = "No se ha encontrado ninguna camara."; return false; }

    IMFMediaSource* source = nullptr;
    HRESULT hr = cam->ActivateObject(IID_PPV_ARGS(&source));
    cam->Release();
    if (FAILED(hr) || !source) {
        // Aqui cae tambien el caso de permiso de camara denegado en Windows.
        error = "No se pudo abrir la camara (revisa los permisos).";
        return false;
    }

    IMFAttributes* rattrs = nullptr;
    IMFSourceReader* reader = nullptr;
    if (SUCCEEDED(MFCreateAttributes(&rattrs, 1))) {
        rattrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
        hr = MFCreateSourceReaderFromMediaSource(source, rattrs, &reader);
        rattrs->Release();
    } else {
        hr = E_FAIL;
    }
    if (FAILED(hr) || !reader) {
        source->Shutdown(); source->Release();
        error = "No se pudo leer de la camara.";
        return false;
    }

    // Pedir RGB32: asi el fotograma llega listo para guardarlo como BMP.
    IMFMediaType* type = nullptr;
    if (SUCCEEDED(MFCreateMediaType(&type))) {
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE,    MFVideoFormat_RGB32);
        hr = reader->SetCurrentMediaType(
                 static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
        type->Release();
    }
    if (FAILED(hr)) {
        reader->Release(); source->Shutdown(); source->Release();
        error = "La camara no admite el formato de imagen necesario.";
        return false;
    }

    // Resolucion real negociada.
    i32 w = 0, h = 0;
    IMFMediaType* cur = nullptr;
    if (SUCCEEDED(reader->GetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &cur)) && cur) {
        UINT32 uw = 0, uh = 0;
        MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &uw, &uh);
        w = static_cast<i32>(uw);
        h = static_cast<i32>(uh);
        cur->Release();
    }
    if (w <= 0 || h <= 0) {
        reader->Release(); source->Shutdown(); source->Release();
        error = "No se pudo determinar el tamano de la imagen.";
        return false;
    }

    // Las primeras lecturas suelen venir vacias mientras la camara arranca.
    bool guardado = false;
    for (int intento = 0; intento < 30 && !guardado; ++intento) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) break;
        if (!sample) { Sleep(50); continue; }

        IMFMediaBuffer* buf = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen))) {
                const size_t need = static_cast<size_t>(w) * h * 4;
                if (curLen >= need) {
                    std::vector<u32> px(static_cast<size_t>(w) * h);
                    // MF entrega RGB32 bottom-up: se invierte al copiar.
                    for (i32 y = 0; y < h; ++y) {
                        const BYTE* srcRow = data + static_cast<size_t>(h - 1 - y) * w * 4;
                        for (i32 x = 0; x < w; ++x) {
                            const BYTE b = srcRow[x * 4 + 0];
                            const BYTE g = srcRow[x * 4 + 1];
                            const BYTE r = srcRow[x * 4 + 2];
                            px[static_cast<size_t>(y) * w + x] =
                                0xFF000000u | (static_cast<u32>(r) << 16) |
                                (static_cast<u32>(g) << 8) | b;
                        }
                    }
                    guardado = saveBmp32(dest, px, w, h);
                }
                buf->Unlock();
            }
            buf->Release();
        }
        sample->Release();
    }

    reader->Release();
    source->Shutdown();
    source->Release();

    if (!guardado) error = "No se pudo capturar la imagen de la camara.";
    return guardado;
}

} // namespace photo
} // namespace ludora
