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

// Playlist
inline bool selectionFollowsPlayback() { return get("playlist/selectionFollows", true); }
inline bool shiftSkipsTransitions() { return get("playlist/shiftCut", true); }
inline double imageDuration() { return get("playlist/imageDuration", 10.0); }
inline double defaultFade() { return get("playlist/defaultFade", 1.0); }
inline bool newCuesPauseAtEnd() { return get("playlist/newPauseAtEnd", false); }

// Áudio
inline QString audioDevice() { return get("audio/device", QString()); }
inline double normalizeTarget() { return get("audio/normalizeTarget", -16.0); }

// Motor e saídas
inline bool vsync() { return get("engine/vsync", true); }
inline int previewFps() { return get("engine/previewFps", 30); }
inline QString enginePriority() { return get("engine/priority", QStringLiteral("above_normal")); }

// Countdown
inline double countdownWarnSeconds() { return get("countdown/warnSeconds", 30.0); }   // laranja
inline double countdownDangerSeconds() { return get("countdown/dangerSeconds", 10.0); } // vermelho
inline bool countdownWebEnabled() { return get("countdown/webEnabled", true); }
inline int countdownWebPort() { return get("countdown/webPort", 8088); }

// Show Mode
inline bool confirmLeaveShowMode() { return get("show/confirmLeave", true); }

inline vthost::protocol::EngineSettings engineSettings() {
    vthost::protocol::EngineSettings s;
    s.vsync = vsync();
    s.previewFps = previewFps();
    s.priority = enginePriority().toStdString();
    s.audioDevice = audioDevice().toStdString();
    return s;
}

} // namespace prefs
