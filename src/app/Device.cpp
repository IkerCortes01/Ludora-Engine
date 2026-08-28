#include "app/Device.h"
#include <vector>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace ludora {

namespace {

std::string toHex(const u8* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0x0F]);
        out.push_back(kHex[ data[i]       & 0x0F]);
    }
    return out;
}

std::string sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};

    DWORD hashLen = 0, cb = 0;
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &cb, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    std::vector<u8> digest(hashLen);
    std::string out;
    if (BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), 0) >= 0 &&
        BCryptFinishHash(h, digest.data(), hashLen, 0) >= 0) {
        out = toHex(digest.data(), digest.size());
    }

    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

/// MachineGuid: identificador que Windows genera al instalarse. Es estable
/// entre reinicios y no contiene datos personales ni de ubicacion. Solo se usa
/// como material para el hash; nunca se guarda ni se muestra.
std::string machineGuid() {
    HKEY key = nullptr;
    // KEY_WOW64_64KEY: en un proceso de 32 bits la vista redirigida no tiene
    // esta clave, y la huella cambiaria segun como se compile el ejecutable.
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};

    wchar_t buf[128]{};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LSTATUS st = RegQueryValueExW(key, L"MachineGuid", nullptr, &type,
                                        reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(key);

    if (st != ERROR_SUCCESS || type != REG_SZ) return {};

    std::string out;
    for (const wchar_t* p = buf; *p; ++p)
        out.push_back(static_cast<char>(*p & 0x7F));   // el GUID es ASCII
    return out;
}

/// Segundo factor de la huella: el volumen del disco del sistema. Refuerza el
/// vinculo con el equipo. Es un numero de formateo, no una ubicacion.
std::string volumeSerial() {
    wchar_t root[MAX_PATH]{};
    if (!GetSystemDirectoryW(root, MAX_PATH)) return {};
    // Quedarse con "C:\" a partir de la ruta del sistema.
    if (root[1] != L':') return {};
    const wchar_t drive[4] = { root[0], L':', L'\\', L'\0' };

    DWORD serial = 0;
    if (!GetVolumeInformationW(drive, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
        return {};

    char tmp[32]{};
    _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%08lX", serial);
    return tmp;
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

} // namespace

Device::Kind Device::kind() {
    // GetSystemPowerStatus distingue con bateria (portatil) de sin ella
    // (escritorio). Es una senal del TIPO de equipo, no de cual es.
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        // 128 = "sin bateria del sistema".
        if (sps.BatteryFlag != 128 && sps.BatteryFlag != 255)
            return Kind::Laptop;
    }

    // Un equipo con entrada tactil integrada y bateria se trata como tableta.
    const int digitizer = GetSystemMetrics(SM_DIGITIZER);
    if (digitizer & 0x00000080 /*NID_INTEGRATED_TOUCH*/) {
        SYSTEM_POWER_STATUS p{};
        if (GetSystemPowerStatus(&p) && p.BatteryFlag != 128 && p.BatteryFlag != 255)
            return Kind::Tablet;
    }

    return Kind::Desktop;
}

const char* Device::kindName(Kind k) {
    switch (k) {
    case Kind::Desktop: return "Escritorio";
    case Kind::Laptop:  return "Portatil";
    case Kind::Tablet:  return "Tableta";
    default:            return "Desconocido";
    }
}

std::string Device::fingerprint(const std::string& salt) {
    // El material se combina y se hashea aqui mismo: `raw` es local y muere
    // al salir, asi que los identificadores en claro nunca llegan al disco.
    const std::string raw = salt + "|" + machineGuid() + "|" + volumeSerial();
    return sha256Hex(raw);
}

bool Device::matches(const std::string& salt, const std::string& storedHash) {
    if (storedHash.empty()) return false;
    const std::string calc = fingerprint(salt);
    if (calc.empty()) return false;
    return constantTimeEquals(calc, storedHash);
}

} // namespace ludora
