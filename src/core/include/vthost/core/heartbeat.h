#pragma once
// Bloco de memória partilhada (Local\VTHOST_Heartbeat) entre watchdog e motor.
// O motor incrementa `counter` a cada frame. Se o contador parar (motor
// bloqueado, driver pendurado...) o watchdog mata e relança o motor.

#include <cstdint>

namespace vthost {

inline constexpr std::uint32_t kHeartbeatMagic = 0x56544854; // "VTHT"

enum class EngineRunState : std::uint32_t {
    Starting = 0,
    Running = 1,
    StoppingOnPurpose = 2, // shutdown pedido: não relançar
};

struct HeartbeatBlock {
    std::uint32_t magic;
    std::uint32_t enginePid;
    std::uint64_t counter;       // incrementado a cada frame
    std::uint32_t state;         // EngineRunState
    std::uint32_t restarts;      // escrito pelo watchdog (nº de recuperações)
};

} // namespace vthost
