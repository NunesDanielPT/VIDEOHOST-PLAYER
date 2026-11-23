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
inline bool autoStartEngine() { return get("general/autoStartEngine", true); }
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
inline QString countdownBindAddress() { return get("countdown/bindAddress", QString()); } // "" = todas as placas

// OSC (Bitfocus Companion). Desligado por omissão: só controla quem o utilizador escolher.
inline bool oscEnabled() { return get("osc/enabled", false); }
inline QString oscBindAddress() { return get("osc/bindAddress", QString()); } // "" = todas as placas
inline int oscPort() { return get("osc/port", 51000); }
inline QString oscFeedbackHost() { return get("osc/feedbackHost", QString()); }
inline int oscFeedbackPort() { return get("osc/feedbackPort", 51001); }
inline bool oscOnlyFromFeedbackHost() { return get("osc/onlyFromFeedbackHost", true); }

// Misturador (vMix / Blackmagic ATEM): dispara a cue armada (CUE) quando o input do player vai para o ar.
inline int tallyMode() { return get("tally/mode", 0); } // 0 desligado, 1 vMix, 2 ATEM
inline QString tallyHost() { return get("tally/host", QString()); }
inline int tallyInput() { return get("tally/input", 1); }
inline bool tallyArmNext() { return get("tally/armNext", true); }

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
