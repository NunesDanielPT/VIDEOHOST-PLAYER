# Arquitetura do VTHOST

```
┌──────────────────┐   named pipe (JSON por linha)   ┌───────────────────┐
│  vthost-studio   │ ──────────────────────────────▶ │   vthost-engine   │──▶ Output 1 (monitor 1)
│  (Qt 6, edição)  │ ◀────────────────────────────── │ (FFmpeg + D3D11)  │──▶ Output 2 (monitor 2)
└──────────────────┘                                 └───────────────────┘
        │ lança (startDetached)                         ▲   │ heartbeat (memória partilhada)
        ▼                                               │   ▼ show_state.json (2×/s, atómico)
┌──────────────────┐   lança / relança --recover       │
│ vthost-watchdog  │ ──────────────────────────────────┘
└──────────────────┘
```

## Processos

**Studio** (`src/studio`): interface. Mantém o projeto em memória; a cada alteração escreve uma cópia ao vivo (`%LOCALAPPDATA%\VTHOST\live\live.vthost`) e envia `load_project` ao motor. O ficheiro do utilizador só muda ao *Guardar*. Se o Studio arrancar com um show já a correr, adota esse show em vez de o substituir.

**Watchdog** (`src/watchdog`): lança o motor e vigia-o.
- Motor sai com código ≠ 0 ou crasha → relança com `--recover`.
- Heartbeat parado durante 4 s (com 20 s de tolerância no arranque) → `TerminateProcess` + relança.
- Motor sai com `0` e estado `StoppingOnPurpose` → o watchdog termina também.
- Mais de 5 arranques num minuto → espera 5 s entre tentativas, mas nunca desiste.

**Motor** (`src/engine`): sem interface.
- `VideoDecoder`: uma thread por media, FFmpeg, fila limitada a 3 frames, interrupt callback para parar mesmo a meio de uma leitura bloqueada, erros → estado `Error` (nunca excepção para fora).
- `LayerPlayer`: media atual + anterior durante o fade. O relógio de cada media só arranca quando o primeiro frame existe. Sem frame novo → fica o último (frame hold). Descodificadores retirados são destruídos noutra thread para não haver soluços.
- `Renderer`: D3D11, canvas com alfa pré-multiplicado, blend normal/add/multiply/screen; fallback para WARP.
- `OutputWindow`: janela sem bordas por monitor, sem cursor, ignora Alt+F4, bloqueia protetor de ecrã; se o monitor faltar abre uma pré-visualização e volta ao sítio quando o monitor reaparece.
- `IpcServer`: named pipe local (`PIPE_REJECT_REMOTE_CLIENTS`), comandos executados no render loop através de uma fila.
- Recuperação: `show_state.json` tem projeto, Show Mode, coluna ativa, media/posição/opacidade de cada layer.

**Núcleo** (`src/core`): portátil, sem Windows nem Qt, testado em Linux com ASan/UBSan e em Windows. Projeto (parser tolerante), protocolo, escrita atómica, log, estado do show.

## Canvas e mapping

Cada canvas é um render target com a sua resolução; as layers compõem-se no canvas a que pertencem. Cada saída desenha uma **região** de um canvas através de uma malha de 48×48 quads (`buildWarpGrid`, `src/core/mapping.cpp`): perspetiva por homografia (4 cantos) composta com uma grelha de pontos interpolada por Catmull-Rom (pontos-fantasma nas bordas: grelha regular = identidade exata). O shader de saída aplica soft-edge por lado (curva em S + gama, as duas metades somam 1 em luz linear), brilho e, se ligado, o test card procedural no espaço do canvas — contínuo entre saídas, para alinhar projetores. O editor de mapping envia `set_output_mapping` ao vivo; o motor só reconstrói a malha da saída, sem recarregar o projeto.

## Saídas

| Tipo | Como funciona |
|---|---|
| Ecrã | Janela sem bordas no ecrã escolhido. O ecrã é identificado pelo nome de dispositivo (`\\.\DISPLAY2`), não pela ordem, por isso não salta para outro ecrã se o Windows reordenar. Nome do monitor via DisplayConfig (EDID), placa via DXGI. |
| Janela | Janela normal redimensionável (testes, monitorização). |
| NDI | O canvas em resolução total é copiado da GPU de forma assíncrona e enviado numa thread própria com caixa de correio de 1 frame. O runtime NDI (NDI Tools) é carregado em tempo de execução; sem ele, a saída fica em erro e o resto continua. |
| Blackmagic | Em desenvolvimento (SDK DeckLink). |

O Studio pede a lista de ecrãs ao motor (`list_displays`); se o motor estiver desligado usa os ecrãs vistos pelo Qt.

## Preview (Workspace)

O motor reduz o canvas para no máximo 960×540, copia-o para a CPU de forma assíncrona (anel de 3 texturas staging, `D3D11_MAP_FLAG_DO_NOT_WAIT`, nunca espera pela GPU) e escreve-o a ~30 fps no bloco partilhado `Local\VTHOST_Preview` (`src/core/include/vthost/core/preview.h`). A sincronização é um seqlock: o motor nunca espera pelo Studio; o Studio descarta cópias apanhadas a meio. É exatamente a imagem que sai para os outputs.

## Timeline

Cada projeto tem timelines com clips (layer, media, início, duração, ponto de entrada) e segmentos (com ou sem loop). Quando uma timeline está ativa, é ela que decide o que toca em cada layer: em cada frame o motor vê que clip está sob a cabeça de leitura e, se mudou, arranca a media no ponto certo. Pausa congela o relógio das media (frame hold). Um GO no Dashboard sai do modo timeline. O estado da timeline (id, tempo, a tocar) também é gravado para recuperação.

## Atualizações

1. Ao arrancar (opção nas Definições) ou em *Ajuda → Procurar atualizações*, o Studio lê `api.github.com/repos/NunesDanielPT/VTHOST/releases/latest`.
2. Se a tag (`vX.Y.Z`) for mais recente que a versão instalada, mostra as notas e oferece *Atualizar agora / Mais tarde / Ignorar esta versão*.
3. Descarrega `VTHOST-Setup-X.Y.Z.exe` (só HTTPS, só domínios do GitHub) e compara o SHA-256 com o `digest` da release ou com `VTHOST-Setup-X.Y.Z.exe.sha256`. Sem hash confirmado não instala.
4. Pede ao motor um `shutdown` limpo, lança o instalador em modo silencioso com `/RELAUNCH=1` e fecha. O instalador pára watchdog/motor/Studio, substitui os ficheiros e reabre o Studio.
5. Nunca atualiza com o Show Mode ligado.

O instalador é por utilizador (`%LOCALAPPDATA%\Programs\VTHOST`), por isso as atualizações não pedem permissões de administrador. Os dados (`%LOCALAPPDATA%\VTHOST`) nunca são apagados.

## Códigos de saída do motor

| Código | Significado | Watchdog |
|---|---|---|
| 0 | shutdown pedido | termina |
| 3 | demasiados erros seguidos no render loop | relança com recuperação |
| 4 | GPU/driver perdido | relança com recuperação |
| 5 | falha a iniciar | relança (com backoff) |
| 10 | já existe outro motor | tenta outra vez em 3 s |
| outro | crash (minidump escrito) | relança com recuperação |

## Protocolo

Ver `src/core/include/vthost/core/protocol.h`. Uma mensagem JSON por linha, um pedido → uma resposta.

## Regras do projeto

1. Nada no caminho do render pode lançar excepções para fora nem bloquear (I/O e descodificação vivem noutras threads).
2. Memória limitada: filas de frames com tamanho fixo.
3. Ficheiros sempre gravados de forma atómica.
4. Interface em PT e EN: qualquer texto novo no Studio usa `tr()` e é traduzido em `translations/vthost_studio_pt_PT.ts`.
