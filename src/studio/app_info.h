#pragma once
// Identidade da aplicação e origem das atualizações.

#include <QString>

namespace appinfo {

inline const QString kName = QStringLiteral("VTHOST");
inline const QString kVersion = QStringLiteral(VTHOST_VERSION);
inline const QString kAuthor = QStringLiteral("Daniel Nunes");
inline const QString kAuthorUrl = QStringLiteral("https://github.com/NunesDanielPT");

// Repositório de onde vêm as atualizações (GitHub Releases).
// Se o repositório do código passar a privado, criar um repositório público só
// com as releases (ex. NunesDanielPT/VTHOST-releases) e mudar aqui.
inline const QString kUpdateRepo = QStringLiteral("NunesDanielPT/VTHOST");
inline const QString kRepoUrl = QStringLiteral("https://github.com/") + kUpdateRepo;
inline const QString kLatestReleaseApi =
    QStringLiteral("https://api.github.com/repos/") + kUpdateRepo + QStringLiteral("/releases/latest");
// Lista das últimas releases (para mostrar as novidades de todas as versões que faltam).
inline const QString kReleasesApi =
    QStringLiteral("https://api.github.com/repos/") + kUpdateRepo + QStringLiteral("/releases?per_page=30");

// Nome do instalador publicado em cada release: VTHOST-Setup-<versão>.exe
inline const QString kInstallerPrefix = QStringLiteral("VTHOST-Setup-");

} // namespace appinfo
