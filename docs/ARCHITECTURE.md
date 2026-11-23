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

**Motor** (`src/engine`): sem interface. Desde a 0.7.0 é um **player de playlist** (estilo Mitti).
- `VideoDecoder`: uma thread por cue, FFmpeg, fila limitada a 3 frames, zona In..Out repetida N vezes (`DecodeRange`), interrupt callback para parar mesmo a meio de uma leitura bloqueada, erros → estado `Error` (nunca excepção para fora). Ficheiros só de áudio → `NoVideo` (imagem preta).
- `AudioDecoder`: abre o mesmo ficheiro à parte (um problema no som nunca pára a imagem), converte para float estéreo 48 kHz com a velocidade aplicada (reamostragem), fila limitada a ~1 s com o tempo de media de cada bloco.
- `AudioOutput`: WASAPI partilhado, thread "Pro Audio", mistura a cue no ar e a anterior (crossfade) com rampas de ganho por período, medidores L/R. Placa desligada → tenta reabrir de segundo a segundo; o vídeo nunca espera pelo som.
- `MediaClock`: relógio de cada cue (tempo de media = base + tempo real × velocidade, pausa). Imagem e som leem o mesmo relógio: o som alinha-se lendo o bloco do tempo pedido (salta/insere silêncio se o desvio passar de 30 ms).
- `CuePlayer`: cue no ar + anterior durante o crossfade/fade para preto. Fade in/out (por tempo da cue), crossfade (tempo real), pausa no início/fim, saltos (`seek`, GOTO 30/20/10). O relógio só arranca no 1.º frame. Sem frame novo → fica o último (frame hold). Descodificadores retirados são destruídos no `reaper` (outra thread).
- Fim de cue (`EngineApp::handleCueEnd`): pausa no fim → fica no último frame; apagar no fim → preto; senão "ir para" ou a cue seguinte (respeita "repetir a playlist"). Proteção: nada avança antes de 250 ms (ficheiros estragados que "acabam" logo).
- `Renderer`: D3D11, um canvas com a resolução do projeto; a imagem da cue é desenhada com ajuste/escala/posição. Fallback para WARP.
- `OutputWindow`: janela sem bordas por monitor, sem cursor, ignora Alt+F4, bloqueia protetor de ecrã; o canvas aparece com a proporção certa (barras pretas); se o monitor faltar abre uma pré-visualização e volta ao sítio quando o monitor reaparece.
- `IpcServer`: named pipe local (`PIPE_REJECT_REMOTE_CLIENTS`), comandos executados no render loop através de uma fila.
- Recuperação: `show_state.json` tem projeto, Show Mode, test card, cue no ar, tempo de cue e pausa.

**Núcleo** (`src/core`): portátil, sem Windows nem Qt, testado em Linux com ASan/UBSan e em Windows. Projeto (parser tolerante, converte projetos 0.6), protocolo, medidor de loudness (BS.1770 / EBU R128), countdown, escrita atómica, log, estado do show.

## Projeto (formato 2)

`cues`: id estável (`Q1`...), ID visível, nome, ficheiro, cor, notas, In/Out, velocidade (25–400 %), repetições (0 = infinito), pausa no início/fim, apagar no fim, fade in/out, crossfade, "ir para", mute, volume (dB), normalização (LUFS e pico medidos pelo Studio) e geometria (ajuste, escala, X/Y). `outputs`: ecrã, janela ou NDI. Também: resolução, repetir a playlist, volume geral.

Ganho de áudio de uma cue = volume + normalização (alvo − LUFS medidos, ±20 dB, limitado para o pico não passar de −1 dBFS) + volume geral, vezes os fades.

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

## Studio (estilo Mitti)

Barra de transporte (play/pausa, parar, anterior/seguinte, repetir playlist, GOTO 30/20/10, código de tempo, volume geral e medidor), playlist (miniatura, número/ID, cor, Start/TRT, ícones de comportamento clicáveis, arrastar ficheiros e reordenar), preview com tempo decorrido e restante, forma de onda (In/Out arrastáveis, clicar para saltar) e Inspector. A análise das media (`MediaCache`) corre em threads de fundo: miniatura/info num pool de 2 threads; forma de onda + loudness numa thread de prioridade baixa (nunca rouba CPU ao show).

## Atualizações

1. Ao arrancar, de 30 em 30 minutos (opção nas Definições) ou em *Ajuda → Procurar atualizações*, o Studio lê `api.github.com/repos/NunesDanielPT/VTHOST/releases` e junta as novidades de todas as versões mais recentes do que a instalada. A janela "Nova versão disponível" abre sozinha uma vez por versão, nunca em Show Mode nem com uma cue no ar (fica o aviso no rodapé). As notas de cada release são a mensagem do commit da versão (a CI escreve-as).
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
| 6 | reinício pedido (`restart`, ex. outra placa gráfica) | relança já com recuperação |
| 10 | já existe outro motor | tenta outra vez em 3 s |
| outro | crash (minidump escrito) | relança com recuperação |

## Protocolo

Ver `src/core/include/vthost/core/protocol.h`. Uma mensagem JSON por linha, um pedido → uma resposta.

## Regras do projeto

1. Nada no caminho do render pode lançar excepções para fora nem bloquear (I/O e descodificação vivem noutras threads).
2. Memória limitada: filas de frames com tamanho fixo.
3. Ficheiros sempre gravados de forma atómica.
4. Interface em PT e EN: qualquer texto novo no Studio usa `tr()` e é traduzido em `translations/vthost_studio_pt_PT.ts`.

## Blocos

`Project::blocks` (id, nome, escondido, loop, fim = continue/pause/stop) e `Cue::block`. As cues de um bloco ficam sempre seguidas (`normalizeBlocks`, ao abrir e depois de cada edição). `nextCue` volta à primeira cue do bloco quando ele tem loop; no motor, `handleCueEnd` aplica o fim do bloco (pausa no último frame ou preto) quando a última cue acaba. Na playlist, cada bloco é uma linha de cabeçalho (`PlaylistView::cueAt`/`blockAt` convertem linhas em cues).

## Output para DSM

Uma saída com `dsm` desenha o canvas mais pequeno (`computeDsmLayout`, núcleo) e por baixo uma faixa com a duração e o tempo que falta (`computeCountdown`). O texto é desenhado com GDI numa thread (`DsmPanelRenderer`, só quando muda) e o render loop copia a imagem para o canvas `dsm:<saída>` (`uploadCanvas`). Saídas NDI em DSM compõem num canvas `ndidsm:<saída>` 1920x1080.

## Placa gráfica

O Studio grava a escolha em `%LOCALAPPDATA%\VTHOST\engine_config.json` (`engine_config.h`); o motor lê-a ao arrancar e cria o dispositivo D3D11 nessa placa (`gpus.h`: vazio = a de alto desempenho via `IDXGIFactory6`). Se não existir, usa a de alto desempenho e avisa. Mudar de placa = comando `restart` (o motor grava o estado e sai com o código 6; o watchdog relança com `--recover`).

## Misturador (tally)

O Studio liga-se ao vMix (TCP 8099, `SUBSCRIBE TALLY`) ou a um ATEM (UDP 9910: hello, confirmações de cada pacote, comando `TlSr` = tally por fonte) — a parte pura (pacotes, comandos) está no núcleo (`tally.h`, testada). Quando o input do player passa a programa (só transições depois do primeiro estado), toca a cue armada (botão CUE) e, por opção, arma a seguinte. Sem ligação, nada dispara.

## OSC (Companion)

O Studio (não o motor) escuta OSC em UDP (porta 51000, só na placa escolhida, desligado por omissão) e fala o protocolo do Mitti (`/mitti/...`), para o módulo "Imimot Mitti" do Companion funcionar com os seus presets. O estado (cue no ar, tempo que falta, play/pausa, nomes das cues) vai para o IP do Companion (porta 51001) 10×/s, só quando muda. Descodificação OSC no núcleo (`osc.h`, testada com dados aleatórios).

## Countdown

`computeCountdown()` (núcleo, testado) conta o que falta à cue no ar (todas as repetições;
em loop infinito, a passagem actual). O Studio interpola entre estados (`advanceCountdown`)
e mostra-o no preview, na janela de countdown (Ctrl+T) e numa
página web servida pelo próprio Studio (`CountdownServer`, QTcpServer, só GET, máx. 64
ligações, porta 8088 por omissão): `GET /` (página em `src/studio/web/countdown.html`) e
`GET /api/countdown` (JSON). O motor não é tocado: se o Studio fechar, só a página pára.
