# VTHOST — instruções para o Claude

- Autor: Daniel Nunes (github.com/NunesDanielPT). Comunicar em português de Portugal.
- Media server para espetáculos em Windows; referência funcional: Millumin 5. Prioridade número 1: **estabilidade, o output nunca pode cair**.
- Ler `docs/ARCHITECTURE.md` antes de mexer no motor.

## Regras
1. Interface em PT e EN: qualquer texto novo no Studio usa `tr()` (fonte em inglês) e **tem de** ser traduzido em `src/studio/translations/vthost_studio_pt_PT.ts` na mesma alteração.
2. Nada no render loop pode bloquear nem deixar escapar excepções. I/O e descodificação ficam noutras threads.
3. Memória limitada (filas de tamanho fixo), escrita de ficheiros sempre atómica (`writeFileAtomic`).
4. O núcleo (`src/core`) não depende de Windows nem de Qt; tem testes em `tests/` que correm em Linux (ASan/UBSan) e em Windows.
5. Qualquer alteração ao protocolo ou ao formato de projeto atualiza os testes.
6. A CI (`.github/workflows/build.yml`) tem de ficar verde.
7. Atualizar `docs/ROADMAP.md` quando uma funcionalidade fica pronta.
