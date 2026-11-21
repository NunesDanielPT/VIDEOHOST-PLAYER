# Roadmap

Referência funcional: media servers de espetáculo como o Millumin 5. A interface e o código do VTHOST são próprios.

## Fase 1 — MVP (em curso)
- [x] Motor separado + watchdog com heartbeat e recuperação do show
- [x] Descodificação FFmpeg (H.264/H.265/ProRes/HAP via CPU, imagens fixas), loop, frame hold
- [x] Composição D3D11: layers com opacidade e blend modes, fades entre cues
- [x] Várias saídas em ecrã inteiro por monitor
- [x] Studio: biblioteca, grelha layers × colunas, GO / GO seguinte, Show Mode, PT/EN
- [ ] Áudio por layer (WASAPI) com fades
- [x] Posição / escala / rotação por layer (Properties, ao vivo; arrastar no Workspace)
- [x] Miniaturas na Library, no Dashboard e na Timeline
- [x] Workspace com preview em direto do que está no ar (memória partilhada do motor)
- [x] Interface Library / Workspace / Properties / Dashboard + Timeline
- [x] Timeline: clips por layer, segmentos com loop, play/pausa/stop/seek, recuperação após crash
- [x] Instalador (Inno Setup, por utilizador, PT/EN)
- [x] Atualizações automáticas a partir das GitHub Releases (SHA-256 verificado, bloqueadas em Show Mode)
- [x] Definições com secção Sobre (autor, versão, créditos)

## Fase 2 — Desempenho
- [ ] Descodificação por hardware (D3D11VA / NVDEC) sem cópia para a CPU
- [ ] HAP / HAP Q / HAP Alpha descodificado na GPU (texturas DXT/BC)
- [ ] Pré-carregamento das media da coluna seguinte
- [ ] Sincronização de saídas (vsync partilhado)
- [ ] Testes de resistência de 72 h e com ficheiros corrompidos

## Fase 3 — Mapping
- [ ] Transformação de perspetiva por layer e por saída
- [ ] Mesh warp
- [ ] Máscaras
- [ ] Soft-edge blending entre projetores
- [ ] Importar projetos do MapSim (projetores, zonas de blend)

## Fase 4 — Controlo
- [ ] Várias timelines por projeto, timelines disparadas a partir do Dashboard
- [ ] OSC, MIDI, Art-Net/DMX de entrada
- [ ] Timecode LTC / MTC
- [ ] NDI de entrada e saída
- [ ] Bitfocus Companion / Stream Deck

## Fase 5 — Avançado
- [ ] Pixel mapping para LEDs
- [ ] Scripting
- [ ] Players remotos controlados pela rede
- [ ] Licenciamento
