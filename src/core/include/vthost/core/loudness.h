#pragma once
// Medição de loudness segundo a ITU-R BS.1770-4 / EBU R128 (LUFS integrados):
// filtro K (shelving + passa-altos), blocos de 400 ms com 75 % de sobreposição,
// porta absoluta a -70 LUFS e porta relativa a -10 LU. Também mede o pico de amostra.
//
// Usado pelo Studio para normalizar o volume das cues (vídeos mais baixos ou mais
// altos ficam todos ao mesmo nível).

#include <cstddef>
#include <optional>
#include <vector>

namespace vthost {

class LoudnessMeter {
public:
    LoudnessMeter(int sampleRate, int channels);

    // Amostras float intercaladas (-1..1), `frames` frames de `channels` canais.
    void add(const float* interleaved, std::size_t frames);

    // LUFS integrados; nullopt se não houver áudio suficiente (ou só silêncio).
    std::optional<double> integrated() const;
    // Pico de amostra em dBFS (-inf -> nullopt).
    std::optional<double> peakDb() const;
    double seconds() const { return static_cast<double>(totalFrames_) / rate_; }

private:
    struct Biquad {
        double b0, b1, b2, a1, a2;
        double z1 = 0.0, z2 = 0.0;
        double process(double x) {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    void finishBlockStep();

    int rate_;
    int channels_;
    std::vector<Biquad> stage1_, stage2_; // por canal
    std::size_t stepFrames_;              // 100 ms
    std::size_t stepCount_ = 0;           // frames no passo actual
    std::vector<double> stepSum_;         // soma dos quadrados por canal, no passo actual
    std::vector<std::vector<double>> steps_; // últimos 4 passos (para um bloco de 400 ms)
    std::vector<double> blocks_;          // loudness média (energia) de cada bloco
    double peak_ = 0.0;
    std::size_t totalFrames_ = 0;
};

} // namespace vthost
