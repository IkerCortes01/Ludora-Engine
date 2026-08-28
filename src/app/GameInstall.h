#pragma once
#include "core/Types.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace ludora {

/// Instala un juego copiando su carpeta a un directorio propio de Ludora y
/// dejando constancia en la biblioteca. La copia corre en un hilo aparte para
/// no congelar la interfaz, y publica el progreso 0..1 de forma atomica.
class GameInstall {
public:
    ~GameInstall();

    /// Arranca la copia de la carpeta que contiene `sourceExe` hacia
    /// `destDir`. No bloquea: el progreso se consulta con progress().
    /// El nombre visible del juego es `name`, el ejecutable `exeName`.
    bool start(const std::wstring& sourceExe,
               const std::wstring& destDir,
               const std::string&  name,
               const std::wstring& exeName);

    bool  running()  const { return m_running.load(); }
    bool  done()     const { return m_done.load(); }
    bool  failed()   const { return m_failed.load(); }
    f32   progress() const { return m_progress.load(); }   // 0..1
    const std::string& error() const { return m_error; }

    /// Ruta prevista del ejecutable tras descifrar (valida tras done()).
    std::wstring installedExe() const { return m_installedExe; }
    /// Paquete cifrado (.tomate) que contiene el juego.
    std::wstring vaultPath() const    { return m_vaultPath; }

    void join();

private:
    void worker(std::wstring sourceExe, std::wstring destDir, std::wstring exeName);

    std::thread        m_thread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_done{false};
    std::atomic<bool>  m_failed{false};
    std::atomic<float> m_progress{0.0f};
    std::string        m_error;
    std::wstring       m_installedExe;
    std::wstring       m_vaultPath;   // paquete cifrado del juego
    std::wstring       m_exeName;     // nombre del exe dentro del paquete
};

/// Empaqueta y cifra una carpeta de juego para guardarla en la Nube Tomate.
/// El cifrado es el del Vault (Argon2id + 10 capas AES); el secreto es la
/// huella de este equipo, asi que el paquete solo se abre aqui.
namespace gamevault {

/// Cifra toda la carpeta de `sourceExe` en un unico archivo `destVault`.
/// Publica el progreso 0..1 por `progress` (puntero atomico opcional).
bool sealFolder(const std::wstring& sourceExe,
                const std::wstring& destVault,
                std::atomic<float>* progress,
                std::string& error);

/// Descifra un paquete a una carpeta temporal y devuelve la ruta del exe.
/// `exeName` es el nombre del ejecutable dentro del paquete.
bool openToFolder(const std::wstring& srcVault,
                  const std::wstring& destDir,
                  const std::wstring& exeName,
                  std::wstring& outExe,
                  std::string& error);

} // namespace gamevault

/// Registro de juegos instalados (biblioteca). Persistente en disco.
namespace library {

struct Entry {
    std::string  name;    // nombre visible
    std::wstring exe;     // exe ya instalado en claro ("" si esta cifrado)
    std::wstring vault;   // paquete cifrado .tomate ("" si esta en claro)
    std::wstring exeName; // nombre del exe dentro del paquete cifrado
};

/// Descifra el juego (si esta cifrado) y lo lanza. `tempDir` es donde se
/// descifra. Si `outProc` no es nulo, recibe el handle del proceso lanzado
/// (el llamante lo cierra con CloseHandle). Devuelve false y llena `error`.
bool launchEntry(const Entry& e, const std::wstring& tempDir, std::string& error,
                 void** outProc = nullptr);

/// Carga la lista de juegos instalados.
std::vector<Entry> load(const std::wstring& path);

/// Anade un juego (o actualiza si ya existe por nombre) y guarda.
bool add(const std::wstring& path, const Entry& e);

/// true si ya hay un juego con ese nombre.
bool has(const std::wstring& path, const std::string& name);

/// Lanza el ejecutable de un juego instalado.
bool launch(const std::wstring& exe);

} // namespace library
} // namespace ludora
