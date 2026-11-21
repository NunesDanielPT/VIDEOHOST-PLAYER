#pragma once
// Preferências do VTHOST Studio (QSettings). Um só sítio para as chaves e os
// valores por omissão, usados pelo diálogo de Preferências e pela aplicação.

#include "vthost/core/protocol.h"

#include <QSettings>
#include <QString>

namespace prefs {

template <typename T>
inline T get(const char* key, const T& def) {
    return QSettings().value(QString::fromLatin1(key), QVariant::fromValue(def)).template value<T>();
}
template <typename T>
inline void set(const char* key, const T& v) {
    QSettings().setValue(QString::fromLatin1(key), QVariant::fromValue(v));
}

// Geral
inline bool openLastProject() { return get("general/openLastProject", true); }
inline bool autoStartEngine() { return get("general/autoStartEngine", false); }
inline bool confirmQuit() { return get("general/confirmQuit", true); }

// Workspace
inline bool originTopLeft() { return get("workspace/originTopLeft", false); }
inline bool showLayerOutline() { return get("workspace/showLayerOutline", true); }

// Board (Dashboard)
inline bool showColumnIndex() { return get("board/showColumnIndex", true); }
inline bool loopBoard() { return get("board/loopBoard", false); }
inline bool shiftSkipsTransitions() { return get("board/shiftSkipsTransitions", true); }
inline double defaultColumnFade() { return get("board/defaultFade", 1.0); }

// Timeline
inline bool timelineSnap() { return get("timeline/snap", true); }
inline double imageClipDuration() { return get("timeline/imageDuration", 10.0); }
inline bool followPlayhead() { return get("timeline/followPlayhead", true); }

// Motor e saídas
inline bool vsync() { return get("engine/vsync", true); }
inline int previewFps() { return get("engine/previewFps", 30); }
inline QString enginePriority() { return get("engine/priority", QStringLiteral("above_normal")); }

// Show Mode
inline bool confirmLeaveShowMode() { return get("show/confirmLeave", true); }

inline vthost::protocol::EngineSettings engineSettings() {
    vthost::protocol::EngineSettings s;
    s.vsync = vsync();
    s.previewFps = previewFps();
    s.priority = enginePriority().toStdString();
    s.loopBoard = loopBoard();
    return s;
}

} // namespace prefs
