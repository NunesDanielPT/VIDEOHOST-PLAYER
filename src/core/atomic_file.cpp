#include "vthost/core/atomic_file.h"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace vthost {

bool writeFileAtomic(const fs::path& path, const std::string& content, bool keepBackup,
                     std::string* error) {
    try {
        std::error_code ec;
        if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);

        fs::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) {
                if (error) *error = "não foi possível abrir " + tmp.string();
                return false;
            }
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            out.flush();
            if (!out) {
                if (error) *error = "falha a escrever " + tmp.string();
                return false;
            }
        }

        if (keepBackup && fs::exists(path, ec)) {
            fs::path bak = path;
            bak += ".bak";
            fs::copy_file(path, bak, fs::copy_options::overwrite_existing, ec);
            // Falhar o backup não impede a gravação principal.
        }

        fs::rename(tmp, path, ec); // MSVC: MoveFileEx com REPLACE_EXISTING
        if (ec) {
            if (error) *error = "falha a substituir " + path.string() + ": " + ec.message();
            fs::remove(tmp, ec);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    } catch (...) {
        if (error) *error = "erro desconhecido";
        return false;
    }
}

std::optional<std::string> readFileLimited(const fs::path& path, std::size_t maxBytes,
                                           std::string* error) {
    try {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec) {
            if (error) *error = "ficheiro inacessível: " + path.string();
            return std::nullopt;
        }
        if (size > maxBytes) {
            if (error) *error = "ficheiro demasiado grande: " + path.string();
            return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error) *error = "não foi possível abrir " + path.string();
            return std::nullopt;
        }
        std::string data(static_cast<std::size_t>(size), '\0');
        in.read(data.data(), static_cast<std::streamsize>(size));
        data.resize(static_cast<std::size_t>(in.gcount()));
        return data;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    } catch (...) {
        if (error) *error = "erro desconhecido";
        return std::nullopt;
    }
}

} // namespace vthost
