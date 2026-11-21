#pragma once
// Zona de um ficheiro a reproduzir: do ponto In ao Out, `passes` vezes
// (0 = infinito). Imagem e som usam a mesma regra para o tempo acumulado:
//   tempo = tempo no ficheiro + passagem * (Out - In)
// por isso continuam alinhados em cada repetição.

#include <algorithm>
#include <cmath>

namespace vthost {

struct DecodeRange {
    double in = 0.0;
    double out = 0.0; // 0 = até ao fim do ficheiro
    int passes = 1;   // 0 = infinito

    double effectiveOut(double fileDuration) const {
        const double d = (std::isfinite(fileDuration) && fileDuration > 0.0) ? fileDuration : 0.0;
        if (out > in && (d <= 0.0 || out <= d)) return out;
        return d;
    }
    // Duração de uma passagem em tempo de media (0 = desconhecida).
    double passLength(double fileDuration) const {
        const double o = effectiveOut(fileDuration);
        return o > in ? o - in : 0.0;
    }
    // Passagem e tempo dentro do ficheiro para um tempo acumulado.
    void split(double cueTime, double fileDuration, int& pass, double& local) const {
        const double len = passLength(fileDuration);
        pass = 0;
        local = std::max(in, cueTime);
        if (len <= 0.0 || cueTime <= in) return;
        pass = static_cast<int>(std::floor((cueTime - in) / len));
        if (passes > 0) pass = std::min(pass, passes - 1);
        pass = std::max(0, pass);
        local = cueTime - pass * len;
    }
};

} // namespace vthost
