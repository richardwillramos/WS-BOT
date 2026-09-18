# Warspear Online Bot v1.5

Bot de automação para Warspear Online (cliente 32-bit). Funciona via memória do processo do jogo (ReadProcessMemory/WriteProcessMemory). Versão WINDOWS XP (janelinha pequena)
---

## Arquitetura

```
┌─────────────────────────────┐
│  warspear-controller.exe    │
│  (GUI externo)              │
│                             │
│  Aba Connection: conectar   │──RPM──►  warspear.exe
│  Aba Bot: attack/follow     │         (PID detectado)
│  Aba Players: follow player │
│  Aba Mobs: selecionar alvo  │
│  Aba NPCs: listar NPCs      │
│  Hotkeys F1/F3/F4           │
│  Debug Console              │
│  Status bar                 │
└─────────────────────────────┘
```

**Modo de operação:** O controller lê/escreve memória do jogo via `ReadProcessMemory`/`WriteProcessMemory` direto, sem necessidade de DLL. Usa timers internos para automação.

**DLL opcional:** `warspear-bot22.dll` pode ser injetada para ações adicionais via memória compartilhada (`Local\WarspearBotShared`).

---

## Funcionalidades v1.5

| Funcionalidade | Hotkey | Status | Descrição |
|---------------|--------|--------|-----------|
| Auto Attack | **F1** | Funcionando | Ataca mob mais próximo (ou por nome) |
| Follow Player | **F3** | Funcionando | Segue player selecionado via cliques no mapa |
| Auto Loot | **F4** | **NOVO** | Coleta automaticamente de corpos nearby |
| Mob Name Filter | - | **NOVO** | Filtra mobs por nome (ex: "Rato") |
| Debug Console | - | **NOVO** | Janela de log com todos os eventos do bot |
| Injeção de DLL | - | Funcionando | Injeta DLL no processo do jogo |
| Classificação de entidades | - | Funcionando | Player, NPC, Mob, Corpse |
| Seleção de alvo | Duplo-clique | Funcionando | Seleciona mob para atacar |

---

## Hotkeys

| Tecla | Ação |
|-------|------|
| **F1** | Toggle Auto Attack |
| **F2** | Toggle Auto Heal (placeholder) |
| **F3** | Toggle Follow Player |
| **F4** | Toggle Auto Loot |
| **Enter** | Ataca alvo selecionado (no jogo) |

---

## Offsets de Memória

### Cadeia de Ponteiros

```
0x00D387AC  →  sys (System Instance)
sys + 0x14  →  gm (GameManager)
gm  + 0x40  →  lp (LocalPlayer)
gm  + 0x3C  →  entityTree (Header da árvore de entidades)
```

### Offsets de Entidade

| Offset | Tamanho | Descrição |
|--------|---------|-----------|
| `+0x00` | DWORD | VTable (identificador de classe) |
| `+0x10` | DWORD | Raw X (dividir por 65536.0f) |
| `+0x14` | DWORD | Raw Y (dividir por 65536.0f) |
| `+0x58` | DWORD | Ponteiro para nome (UTF-16LE) |
| `+0x060` | DWORD | Tamanho do nome |
| `+0x090` | DWORD | Tipo (1=player, 2=outro) |
| `+0x10C` | DWORD | HP atual |
| `+0x110` | DWORD | HP máximo |
| `+0x114` | DWORD | Mana atual |
| `+0x118` | DWORD | Mana máximo |
| `+0x290` | DWORD | Alvo atual do jogador |
| `+0x478` | DWORD | Alvo secundário |

### Árvore de Entidades (BST)

| Offset | Descrição |
|--------|-----------|
| Node `+0x04` | Left child |
| Node `+0x08` | Right child |
| Node `+0x14` | Object pointer |
| Header `+0x00` | Root node |
| Header `+0x04` | Count |

### VTables (Classificação)

| VTable | Tipo |
|--------|------|
| `0x00C80F9C` | Local Player |
| `0x00C8137C` | NPC (humanoide) |
| `0x00C81490` | Mob (bestiário) |
| `0x00C4FC5C` | **Objeto/Spawn** (corpos, drops, objetos interativos) |

### Offsets de Objeto (spawned)

| Offset | Tamanho | Descrição |
|--------|---------|-----------|
| `+0x120` | WORD | object_id |
| `+0x124` | WORD | type_id / state_id |
| `+0x128` | DWORD | lifetime_ms (0 = infinito) |
| `+0x12C` | DWORD | creation_tick (GetTickCount) |

**Nota:** Objetos (incluindo corpos) usam HP = `0xFFFFFFFE` (-2) como sentinela de "invulnerável".

---

## Identificação de Corpos

Corpos NÃO são entidades regulares na árvore. Quando um mob morre:

1. O mob é removido da árvore de entidades
2. Um **objeto corpse** (vtable `0x00C4FC5C`) é spawnado na mesma posição
3. O corpse tem HP = `0xFFFFFFFE` (sentinel), maxHP > 0
4. Loot data é anexada ao corpse
5. Ao clicar no corpse, o jogo abre o loot UI automaticamente

### Funções Relevantes (Ghidra)

| Endereço | Função | Descrição |
|----------|--------|-----------|
| `0x00963AA0` | `FUN_00963aa0` | Loot selection handler |
| `0x00963C80` | `FUN_00963c80` | Loot initialization |
| `0x00A67580` | `FUN_00a67580` | Server object spawn handler |

### Strings Chave no Binário

| Endereço | String |
|----------|--------|
| `0x00c77bf0` | `"LogicLoot"` |
| `0x00c77bd8` | `"LogicLoot::sel_selector"` |
| `0x00c59bb0` | `"icon_target_corpse"` |
| `0x00c5ccd8` | `"player_corpse_u"` |
| `0x00c59a5c` | `"icon_loot_all"` |

---

## Estrutura do Projeto

```
WS-BOT/
├── controller/
│   ├── main.cpp                ← Controlador GUI (~1030 linhas)
│   ├── build.bat               ← Script de compilação
│   └── warspear-controller.exe ← Compilado (918KB)
│
├── dllmain.cpp                 ← DLL principal (bot injetado)
├── warspear-bot22.dll          ← DLL compilada
├── include/
│   ├── game_memory.h           ← Offsets, estruturas da DLL
│   └── shared_protocol.h       ← Protocolo de memória compartilhada
│
└── README.md                   ← Este arquivo
```

---

## Build

### Pré-requisitos

- Visual Studio 2026 Build Tools (MSVC x86)
- `vcvars32.bat` em `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\`

### Compilar o Controller

```bat
cd WS-BOT\controller
build.bat
```

Ou manualmente:
```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
cl /Od /Zi /EHsc /MT /DUNICODE /D_UNICODE main.cpp /Fe:warspear-controller.exe /link user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib comdlg32.lib psapi.lib
```

### Flags de Compilação

- `/Od` — Sem otimização (debug)
- `/Zi` — Informação de debug
- `/MT` — Runtime estática
- `/DUNICODE /D_UNICODE` — Unicode

---

## Uso

1. Abrir Warspear Online (deixa o jogo carregado)
2. Abrir `warspear-controller.exe`
3. Aba **Connection**: selecionar `warspear.exe` na lista e clicar **CONNECT**
4. (Opcional) Clicar **Inject DLL** para usar a DLL
5. Aba **Bot**:
   - Marcar **Auto Attack** (ou F1) para atacar mobs
   - Digitar nome no campo "Mob name" para filtrar (ex: "Rato")
   - Marcar **Auto Loot** (ou F4) para coletar corpos
6. Aba **Players**: duplo-clique num player para segui-lo (ou F3)
7. Aba **Mobs**: duplo-clique num mob para selecionar como alvo

---

## Debug Console

Clicando **CONNECT**, uma janela de debug abre mostrando todos os eventos:

```
=== Bot Debug Console Started ===
PID: 11528
[CONNECT] SUCCESS - Character: Ligerinh
[CONNECT] Position: (123.4, 456.7) HP: 500/500 Mana: 200/200
[CONNECT] Entities: 2 players, 15 mobs, 8 NPCs, 3 corpses
[ATK] New target: Rato (0x0E5D0C38) HP=100/100
[ATK] Writing target + sending enter attack...
[LOOT] Found 2 corpses nearby
[LOOT] Clicking corpse: Corpse at (120.1, 450.3) dist=7.2
[FOLLOW] target=(130.0, 460.0) self=(123.4, 456.7) dist=8.1
```

---

## Notas Técnicas

### Como o Ataque Funciona

1. O controller escreve o endereço do mob no slot de alvo do jogador (`lp+0x290` e `lp+0x478`)
2. Envia tecla Enter para o jogo (simula o ataque)
3. Timer repete a cada 1500ms

### Como o Follow Funciona

1. Lê posição do player alvo via `ENT_RAW_X` / `ENT_RAW_Y`
2. Calcula direção e distância
3. Clica no mapa na direção do alvo (proporcional: `dist * 0.6f`, máx 15.0f)
4. Timer repete a cada 800ms

### Como o Auto Loot Funciona

1. Percorre a árvore de entidades procurando vtable `0x00C4FC5C` (objetos spawned)
2. Identifica corpos (HP = 0xFFFFFFFE, type_id específico)
3. Clica na posição do corpse no mapa para abrir o loot UI
4. Timer repete a cada 1200ms

### Filtros de Mob

O campo "Mob name" na aba Bot filtra quais mobs atacar:
- Vazio = ataca qualquer mob (exceto NPCs)
- "Rato" = ataca apenas mobs com "Rato" no nome
- Usa `wcsstr()` para substring match

---

## Pendente / Melhorias

| Item | Prioridade | Descrição |
|------|-----------|-----------|
| Follow direto por coordenada | Média | Usar WPM para mover em vez de cliques |
| Auto Heal | Baixa | Ler HP, comparar threshold, usar skill |
| Loot por tecla | Média | Enviar tecla/tab específica para loot |
| UI Corpse List | Baixa | Mostrar lista de corpos na aba |
| Overlay | Baixa | Mostrar HP/distance na tela do jogo |


MEU PIX SE QUISER ME AJUDAR A COMPRAR LEITE PROS MEUS FILHOS 

""  CNPJ : 57944048000175  ""

RICHARD WILLIAN RAMOS - C6
