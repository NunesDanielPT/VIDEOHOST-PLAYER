# Roadmap

Referência funcional: até à 0.6, media servers como o Millumin 5; desde a 0.7.0, players de playlist como o Mitti (só existe para Mac). A interface e o código do VTHOST são próprios.

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
- [x] Properties: ajuste (fit/fill/stretch/nativo), espelhar, corner pin por layer (pegas no Workspace)
- [x] Efeitos de cor por layer: brilho, contraste, saturação, gama, matiz, inverter
- [x] Máscara por layer: recorte de cada lado com contorno suave
- [x] Controlos ao vivo sem atraso (comandos coalescidos, motor processa em lote)
- [ ] Máscaras desenhadas (pontos/bezier) e por saída
- [ ] Mais efeitos: chroma key, LUT, blur, levels, ISF
- [ ] Importar projetos do MapSim (projetores, zonas de blend)

## Fase 3.4 — Cues e edição (0.6.0)
- [x] Definições por cue: transição (dissolve, fade pelo preto, corte) e duração
- [x] Avanço automático por cue: quando os vídeos acabam ou ao fim de X segundos
- [x] "Depois desta cue, ir para": saltos entre cues (ex. da 15 voltar à 3), usados pelo GO e pelo avanço automático
- [x] Barra de tempo no Workspace: decorrido/restante, saltar arrastando, pausa por layer
- [x] Desfazer/refazer (Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y), até 100 passos
- [x] Lista de todos os atalhos nas Preferências
- [x] Countdown (0.6.1): no Workspace, janela própria (ecrã inteiro para o palco) e página web na rede local
- [x] Miniaturas nas células do Dashboard (como no Millumin), com barra de progresso na cue no ar

## Fase 4 — Player de playlist (0.7.0)
- [x] Interface tipo Mitti: barra de transporte, playlist com miniaturas e ícones, preview com tempos, forma de onda, Inspector
- [x] Cues: In/Out, velocidade, repetições, pausa no início/fim, apagar no fim, fade in/out, crossfade, "ir para", cor, ID, notas
- [x] GOTO 30/20/10, repetir a playlist, SHIFT = sem transição
- [x] Som no motor (WASAPI): mistura com crossfade, volume por cue e geral, mute, escolha da placa de som, medidores
- [x] Normalização de loudness (LUFS, EBU R128) com limite de pico
- [x] Removido: layers, Dashboard, Timeline, canvases múltiplos, mapping/warp/soft-edge, efeitos
- [x] Countdown na rede local mantido (agora da cue no ar)
- [x] 0.7.3: workspaces MEDIAPLAYER / CUEPLAYER (CUEPLAYER em desenvolvimento), escolha da placa de rede da página do countdown, menus arrumados (motor dentro de Saídas, arranca sozinho)
- [x] 0.8.1: CUEPLAYER retirado (a 0.8.0 tinha-o; vai ser um software próprio). Fica só o player. Separador Rede nas Preferências, menu Definições antes de Ajuda, countdown no menu Janela
- [x] 0.8.2: VUs L/R ao lado do preview (com pico), preview mostra fps da saída e do vídeo, ID = posição na playlist (escrever um número move a cue)
- [x] 0.7.2: atualizações como no MapSim (janela com as novidades de cada versão, progresso, aviso no rodapé, procura de 30 em 30 min); notas das releases geradas pela CI
- [x] 0.7.1: miniatura escolhida (botão da câmara / Ctrl+P na cue a tocar), miniatura automática evita frames pretos, ícones de transporte claros, ESPAÇO arranca a cue selecionada
- [ ] Seleção de várias cues para alterar em conjunto
- [ ] Legendas (SRT), saída de áudio por canais/placas diferentes por cue
- [ ] Time-stretch sem mudar o tom ao mudar a velocidade

## Fase 5 — Controlo (0.8.0)
- [ ] OSC entrada/saída, MIDI learn, DMX/Art-Net de entrada
- [ ] Várias timelines por projeto, timelines disparadas a partir do Dashboard
- [ ] OSC, MIDI, Art-Net/DMX de entrada
- [ ] Timecode LTC / MTC
- [ ] NDI de entrada e saída
- [ ] Bitfocus Companion / Stream Deck

## Fase 6 — Avançado
- [ ] Pixel mapping para LEDs
- [ ] Scripting
- [ ] Players remotos controlados pela rede
- [ ] Licenciamento
