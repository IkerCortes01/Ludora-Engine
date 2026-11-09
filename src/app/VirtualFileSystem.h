#pragma once
#include "core/Types.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ludora {

// ============================================================================
//  Sistema de Archivos Virtual (VFS) con cifrado por-asset en memoria.
// ============================================================================
//
//  OBJETIVO
//  Los recursos (texturas, modelos, audios) viven CIFRADOS y COMPRIMIDOS
//  dentro de un unico contenedor .pak en disco. El motor pide un asset por su
//  ruta logica ("textures/player.png"), el VFS localiza su bloque, lo lee, lo
//  descifra y lo descomprime DIRECTAMENTE EN RAM, y se lo entrega al motor.
//  El asset descifrado NUNCA se escribe a disco, ni siquiera temporalmente, y
//  se borra de forma segura de la RAM en cuanto el motor termina de usarlo.
//
//  ------------------------------------------------------------------------
//  ESTRUCTURA DEL ARCHIVO CONTENEDOR (.pak)
//  ------------------------------------------------------------------------
//
//   +======================================================================+
//   |  CABECERA (fija, 40 bytes)                                           |
//   |    magic        u32   "LPAK" (0x4B41504C)  -> identifica el formato   |
//   |    version      u32   version del formato (=1)                        |
//   |    flags        u32   bits de opciones (bit0 = XOR en vez de AES)     |
//   |    assetCount   u32   numero de entradas en la tabla de contenidos    |
//   |    tocOffset    u64   offset absoluto de la Tabla de Contenidos       |
//   |    tocSize      u64   tamano en bytes de la Tabla de Contenidos       |
//   |    masterSalt   u8[8] salt para derivar la clave maestra del equipo   |
//   +======================================================================+
//   |  BLOQUES DE DATOS  (uno por asset, en cualquier orden)               |
//   |    [asset 0]  nonce(12) | tag(16) | payload cifrado (comprimido)      |
//   |    [asset 1]  nonce(12) | tag(16) | payload cifrado (comprimido)      |
//   |    ...                                                                |
//   +======================================================================+
//   |  TABLA DE CONTENIDOS (TOC)  en tocOffset, tamano tocSize             |
//   |    por asset:                                                         |
//   |      pathLen   u16   longitud de la ruta logica (utf8)               |
//   |      path      u8[]  ruta logica ("textures/player.png")             |
//   |      offset    u64   offset absoluto del bloque de datos              |
//   |      encSize   u64   tamano del bloque en disco (nonce+tag+payload)   |
//   |      rawSize   u64   tamano del asset ORIGINAL (tras descomprimir)    |
//   +======================================================================+
//
//  El TOC se lee entero al montar y se indexa en una hash-map (ruta -> entry),
//  de modo que buscar un asset es O(1). Los bloques de datos se leen bajo
//  demanda: solo se toca el disco para el asset que se pide.
//
//  ------------------------------------------------------------------------
//  MODELO DE CIFRADO  (decision: por-asset, clave del equipo)
//  ------------------------------------------------------------------------
//  Al MONTAR el .pak se deriva UNA clave maestra de 256 bits, memoria-dura
//  (Argon2id, via Vault::stretchSecret) a partir de la huella del equipo
//  (Device::fingerprint) y masterSalt. Ese coste se paga una sola vez.
//
//  Cada asset se cifra por separado con AES-256-GCM usando esa clave maestra
//  y un nonce unico de 12 bytes por asset. Ventajas frente a cifrar el .pak
//  entero de una vez:
//    - se descifra UN asset sin tocar los demas (streaming, carga perezosa),
//    - cada bloque lleva su propio tag GCM: la integridad se verifica por
//      asset y una manipulacion se detecta antes de subirlo a la GPU/audio,
//    - el Argon2id memoria-duro no se repite cientos de veces.
//
//  Como la clave sale de la huella del equipo, el .pak solo se abre en la
//  maquina donde se instalo: copiarlo a otro PC no permite leer los assets.
// ============================================================================

/// Buffer de bytes descifrados en RAM que se BORRA de forma segura al
/// destruirse (SecureZeroMemory). Es el contenedor con el que el VFS entrega
/// un asset ya en claro al motor: cuando el AssetBuffer sale de ambito, su
/// contenido desaparece de la memoria, sin dejar el asset descifrado por ahi.
///
/// Movable pero NO copiable: un asset descifrado tiene un unico dueno, para
/// que no queden copias residuales del contenido sensible en RAM.
class AssetBuffer {
public:
    AssetBuffer() = default;
    explicit AssetBuffer(std::vector<u8>&& bytes) : m_bytes(std::move(bytes)) {}
    ~AssetBuffer() { wipe(); }

    AssetBuffer(AssetBuffer&& o) noexcept : m_bytes(std::move(o.m_bytes)) {}
    AssetBuffer& operator=(AssetBuffer&& o) noexcept {
        if (this != &o) { wipe(); m_bytes = std::move(o.m_bytes); }
        return *this;
    }
    AssetBuffer(const AssetBuffer&)            = delete;
    AssetBuffer& operator=(const AssetBuffer&) = delete;

    const u8*   data()  const { return m_bytes.data(); }
    size_t      size()  const { return m_bytes.size(); }
    bool        empty() const { return m_bytes.empty(); }

    /// Borra el contenido de la RAM ahora, sin esperar al destructor. Util si
    /// el motor ya subio el asset a la GPU y quiere liberar cuanto antes.
    void wipe();

private:
    std::vector<u8> m_bytes;
};

/// Sistema de archivos virtual: monta un .pak y sirve assets descifrados en RAM.
class VirtualFileSystem {
public:
    /// Entrada de la tabla de contenidos.
    struct Entry {
        std::string path;      // ruta logica
        u64         offset = 0; // offset del bloque de datos en el .pak
        u64         encSize = 0; // tamano del bloque cifrado en disco
        u64         rawSize = 0; // tamano del asset original
    };

    VirtualFileSystem() = default;

    /// Monta el .pak: lee cabecera + TOC y deriva la clave maestra del equipo.
    /// No carga ningun asset todavia. Devuelve false y llena `error` si falla.
    bool mount(const std::wstring& pakPath, std::string& error);

    /// true si `logicalPath` existe en el contenedor.
    bool contains(const std::string& logicalPath) const;

    /// Lee, descifra y descomprime un asset a RAM. El resultado es un
    /// AssetBuffer que se auto-borra al destruirse. Devuelve false y deja
    /// `out` vacio si el asset no existe o la integridad no cuadra.
    bool read(const std::string& logicalPath, AssetBuffer& out, std::string& error) const;

    /// Numero de assets en el contenedor.
    size_t assetCount() const { return m_index.size(); }

    // --- Creacion del contenedor (herramienta de empaquetado) ---------------

    /// Origen de un asset a empaquetar: su ruta logica y sus bytes en claro.
    struct SourceAsset {
        std::string      logicalPath;
        std::vector<u8>  bytes;
    };

    /// Construye un .pak cifrando cada asset con la clave maestra del equipo.
    /// `useXorInstead` activa el modo XOR (solo ofuscacion, NO seguro): existe
    /// para pruebas/demos, jamas para proteger de verdad.
    static bool build(const std::wstring& pakPath,
                      const std::vector<SourceAsset>& assets,
                      bool useXorInstead,
                      std::string& error);

private:
    bool deriveMasterKey(const std::vector<u8>& salt, std::string& error);
    bool decryptBlock(const std::vector<u8>& block, u64 rawSize,
                      std::vector<u8>& out, std::string& error) const;

    std::wstring                                m_pakPath;
    std::vector<u8>                             m_masterKey;   // 32 bytes, en RAM
    bool                                        m_xor = false; // modo ofuscacion
    std::unordered_map<std::string, Entry>      m_index;       // ruta -> entrada
};

} // namespace ludora
