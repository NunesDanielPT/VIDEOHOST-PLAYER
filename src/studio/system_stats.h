#pragma once
// Uso do CPU e da RAM do PC inteiro (para o rodapé do Studio). Leve: uma chamada
// ao sistema por amostra, sem threads.

class SystemStats {
public:
    // false = sem dados (ex. primeira amostra do CPU ou sistema não suportado).
    bool cpuPercent(double& percent);
    bool ram(double& percent, double& usedGb, double& totalGb) const;

private:
    unsigned long long lastIdle_ = 0;
    unsigned long long lastTotal_ = 0;
    bool hasLast_ = false;
};
