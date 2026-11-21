#pragma once
// Identidade da aplicação e origem das atualizações.

#include <QString>

namespace appinfo {

inline const QString kName = QStringLiteral("VIDEOHOST PLAYER"); // antes VTHOST (nomes internos mantêm-se)
inline const QString kVersion = QStringLiteral(VTHOST_VERSION);
inline const QString kAuthor = QStringLiteral("Daniel Nunes");
inline const QString kAuthorUrl = QStringLiteral("https://github.com/NunesDanielPT");

// Repositório de onde vêm as atualizações (GitHub Releases).
// Se o repositório do código passar a privado, criar um repositório público só
// com as releases (ex. NunesDanielPT/VTHOST-releases) e mudar aqui.
inline const QString kUpdateRepo = QStringLiteral("NunesDanielPT/VIDEOHOST-PLAYER");
// Nome antigo do repositório: o GitHub redirecciona-o, mas fica como reserva se o
// novo ainda não existir (404).
inline const QString kLegacyRepo = QStringLiteral("NunesDanielPT/VTHOST");
inline const QString kRepoUrl = QStringLiteral("https://github.com/") + kUpdateRepo;
// Lista das últimas releases (para mostrar as novidades de todas as versões que faltam).
inline QString releasesApi(const QString& repo) {
    return QStringLiteral("https://api.github.com/repos/") + repo + QStringLiteral("/releases?per_page=30");
}

// Nome do instalador publicado em cada release: VIDEOHOST-PLAYER-Setup-<versão>.exe
// (a CI publica também uma cópia VTHOST-Setup-<versão>.exe para as versões antigas).
inline const QString kInstallerPrefix = QStringLiteral("VIDEOHOST-PLAYER-Setup-");
inline const QString kLegacyInstallerPrefix = QStringLiteral("VTHOST-Setup-");

} // namespace appinfo
