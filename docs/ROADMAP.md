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

## Fase 1.5 — Saídas e edição (0.3.0)
- [x] Painel de Saídas: ecrãs detetados com nome do monitor/projetor, resolução, Hz e placa gráfica; Identificar
- [x] Saídas por ecrã (fixas ao ecrã escolhido, mesmo que a ordem do Windows mude), janela e NDI
- [ ] Saída Blackmagic DeckLink / UltraStudio (precisa do SDK Blackmagic)
- [ ] Saída Spout (partilha de textura com Resolume/TouchDesigner no mesmo PC)
- [x] Menus de botão direito em layers, colunas, células, media e clips
- [x] Arrastar layers e colunas para mudar a ordem; olho (mostrar/esconder) e cadeado (bloquear)
- [x] Modos de fim de media: loop, uma vez e congelar, uma vez e parar, auto-follow
- [x] Timeline: cortar clip na cabeça de leitura (Ctrl+Shift+X), fade in/out por clip com pegas
- [x] Logótipo e ícones
- [x] ESPAÇO como no Millumin (coluna seguinte / play-stop da timeline), ALT+ESPAÇO coluna anterior, SHIFT+GO sem transição
- [x] Preferências por separadores: Geral, Workspace, Board, Timeline, Motor e saídas, Show Mode, Atualizações, Sobre
- [ ] Keyframes (animar opacidade/posição/escala/rotação na timeline com curvas)
- [ ] Transições por layer (crossfade / fade-in-and-out) como no Millumin

## Fase 2 — Desempenho
- [ ] Descodificação por hardware (D3D11VA / NVDEC) sem cópia para a CPU
- [ ] HAP / HAP Q / HAP Alpha descodificado na GPU (texturas DXT/BC)
- [ ] Pré-carregamento das media da coluna seguinte
- [ ] Sincronização de saídas (vsync partilhado)
- [ ] Testes de resistência de 72 h e com ficheiros corrompidos

## Fase 3 — Mapping (0.5.0)
- [x] Vários canvas com resolução à medida (favoritos: HD, 4K, 3×HD, vertical…)
- [x] Layers atribuídas a canvas; saídas mostram uma região de um canvas
- [x] Assistente de layout: N×M saídas com sobreposição para soft-edge
- [x] Warp por saída: perspetiva (4 cantos) + grelha de warping (Catmull-Rom), editor ao vivo com setas
- [x] Soft-edge por lado (suavidade, gama) e brilho por saída
- [x] Test card contínuo em todas as saídas
- [x] Solo de layer e barra de ícones das layers (como no Premiere)
- [ ] Máscaras por layer e por saída
- [ ] Warp por layer (perspetiva da media na superfície)
- [ ] Importar projetos do MapSim (projetores, zonas de blend)

## Fase 3.5 — Áudio (0.6.0)
- [ ] Som por layer (WASAPI), escolha da placa de som
- [ ] Volume por media e por layer, mute
- [ ] Normalização: análise de loudness (LUFS) por media e ganho automático

## Fase 4 — Controlo (0.7.0)
- [ ] OSC entrada/saída, MIDI learn, DMX/Art-Net de entrada
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
