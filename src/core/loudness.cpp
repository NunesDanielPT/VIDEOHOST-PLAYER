#include "vthost/core/loudness.h"

#include <algorithm>
#include <cmath>

namespace vthost {
namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

LoudnessMeter::LoudnessMeter(int sampleRate, int channels)
    : rate_(std::clamp(sampleRate, 8000, 384000)), channels_(std::clamp(channels, 1, 16)) {
    // Coeficientes do filtro K para qualquer frequência de amostragem
    // (mesmas fórmulas do libebur128; a 48 kHz dão os valores da norma).
    const double fs = rate_;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b{(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
        stage1_.assign(static_cast<std::size_t>(channels_), b);
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b{1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
        stage2_.assign(static_cast<std::size_t>(channels_), b);
    }
    stepFrames_ = static_cast<std::size_t>(rate_ / 10);
    stepSum_.assign(static_cast<std::size_t>(channels_), 0.0);
}

void LoudnessMeter::add(const float* in, std::size_t frames) {
    if (!in) return;
    const auto ch = static_cast<std::size_t>(channels_);
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t c = 0; c < ch; ++c) {
            double x = in[f * ch + c];
            if (!std::isfinite(x)) x = 0.0;
            peak_ = std::max(peak_, std::abs(x));
            const double y = stage2_[c].process(stage1_[c].process(x));
            stepSum_[c] += y * y;
        }
        if (++stepCount_ >= stepFrames_) finishBlockStep();
    }
    totalFrames_ += frames;
}

void LoudnessMeter::finishBlockStep() {
    // Guarda a energia média de cada canal neste passo de 100 ms.
    std::vector<double> step(stepSum_.size());
    for (std::size_t c = 0; c < stepSum_.size(); ++c) step[c] = stepSum_[c] / static_cast<double>(stepCount_);
    std::fill(stepSum_.begin(), stepSum_.end(), 0.0);
    stepCount_ = 0;
    steps_.push_back(std::move(step));
    if (steps_.size() > 4) steps_.erase(steps_.begin());
    if (steps_.size() < 4) return;
    // Bloco de 400 ms = média dos 4 passos; canais somados (peso 1 para L/R/C).
    double z = 0.0;
    for (std::size_t c = 0; c < stepSum_.size(); ++c) {
        double m = 0.0;
        for (const auto& s : steps_) m += s[c];
        z += m / 4.0;
    }
    blocks_.push_back(z);
}

std::optional<double> LoudnessMeter::integrated() const {
    auto lufs = [](double z) { return -0.691 + 10.0 * std::log10(z); };
    // Porta absoluta.
    double sum = 0.0;
    std::size_t n = 0;
    for (double z : blocks_)
        if (z > 0.0 && lufs(z) > -70.0) {
            sum += z;
            ++n;
        }
    if (n == 0) return std::nullopt;
    // Porta relativa: -10 LU abaixo da média dos blocos que passaram a absoluta.
    const double relGate = lufs(sum / static_cast<double>(n)) - 10.0;
    double sum2 = 0.0;
    std::size_t n2 = 0;
    for (double z : blocks_)
        if (z > 0.0 && lufs(z) > -70.0 && lufs(z) > relGate) {
            sum2 += z;
            ++n2;
        }
    if (n2 == 0) return std::nullopt;
    return lufs(sum2 / static_cast<double>(n2));
}

std::optional<double> LoudnessMeter::peakDb() const {
    if (peak_ <= 0.0) return std::nullopt;
    return 20.0 * std::log10(peak_);
}

} // namespace vthost
