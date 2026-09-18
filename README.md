# Warspear Online Bot v1.5

Bot de automação para Warspear Online (cliente 32-bit). Funciona via memória do processo do jogo (ReadProcessMemory/WriteProcessMemory). 
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

**DLL opcional:** `warspear-bot23.dll` pode ser injetada para ações adicionais via memória compartilhada (`Local\WarspearBotShared`). Usa SetTarget + SendEnter (estilo v1.2).

---

## Funcionalidades v1.5

| Funcionalidade | Hotkey | Status | Descrição |
|---------------|--------|--------|-----------|
| Auto Attack | **F1** | Funcionando | Ataca mob mais próximo (ou por nome) |
| Emergency Stop | **F2** | **NOVO** | Para todos os timers e limpa estado |
| Follow Player | **F3** | Funcionando | Segue player selecionado via cliques no mapa |
| Auto Loot | **F4** | **CORRIGIDO** | Coleta corpos (HP < 0 = morto) com 2 cliques |
| Mob Name Filter | - | Funcionando | Filtra mobs por nome (ex: "Rattlesnake") |
| Debug Console | - | Funcionando | Log de eventos + stats de kills/loots |
| Kill/Loot Counter | - | **NOVO** | Contagem de kills e loots no console e status bar |
| Foreground Safety | - | **NOVO** | Só clica/envia teclas se jogo está em foreground |
| Level/Class Display | - | **NOVO** | Mostra nível e classe no connect popup e status bar |

---

## Hotkeys

| Tecla | Ação |
|-------|------|
| **F1** | Toggle Auto Attack |
| **F2** | **STOP ALL** - Para todos os timers (panic button) + mostra stats |
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
| `+0x2D0` | DWORD | Class ID (1=Seeker,2=Shadow,3=Druid,4=Paladin,5=Mage,6=Necro,7=Tech,8=Assassin) |
| `+0x2E0` | DWORD | Nível do mob/jogador |
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
| `0x00C81490` | Mob (bestiário) - **inclui corpses (HP < 0)** |
| `0x00C4FC5C` | Objeto/Spawn (drops, objetos interativos) |

### Offsets de Objeto (spawned)

| Offset | Tamanho | Descrição |
|--------|---------|-----------|
| `+0x120` | WORD | object_id |
| `+0x124` | WORD | type_id / state_id |
| `+0x128` | DWORD | lifetime_ms (0 = infinito) |
| `+0x12C` | DWORD | creation_tick (GetTickCount) |

**Nota:** Corpos de mobs mantêm a mesma vtable (`0x00C81490`) mas ficam com HP negativo (ex: -24 = `0xFFFFFFE8`). O detection é feito por `HP < 0`.

---

## Identificação de Corpos

Quando um mob morre, o objeto **permanece na entity tree** com a mesma vtable:

1. O mob continua com vtable `0x00C81490` (VT_BEAST) mas o HP fica **negativo**
2. HP negativo = morto (ex: -24 = `0xFFFFFFE8`, -91 = `0xFFFFFFA5`)
3. A posição, nome e dados continuam acessíveis
4. O corpse some da entity tree após alguns segundos
5. Ao clicar no corpse, o jogo abre o loot UI
6. Segundo clique ou Enter confirma "Take All"

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
│   ├── main.cpp                ← Controlador GUI (~1180 linhas)
│   ├── build.bat               ← Script de compilação
│   └── warspear-controller.exe ← Compilado
│
├── dllmain.cpp                 ← DLL principal (SetTarget + SendEnter)
├── warspear-bot23.dll          ← DLL compilada
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
PID: 9292
[CONNECT] SUCCESS - Character: Ligerinhu
[CONNECT] Position: (180.0, 420.0) HP: 1007/1007 Level: 5 Class: Seeker
[CONNECT] Entities: 1 players, 8 mobs, 11 NPCs, 0 corpses
[ATK] New target: Red Rattlesnake (0x0E3866D0) HP=203/203
[ATK] Writing target + sending enter attack...
[ATK] Target dead! Kills=1 name=Red Rattlesnake
[LOOT] 'Red Rattlesnake' dist=1.2 corpse=(183.0,425.0) self=(180.0,420.0)
[LOOT] Clicking corpse at client=(336,250)
[LOOT] Sent Enter to confirm loot
[LOOT] Looted! #1 'Red Rattlesnake' | Total loots: 1 | Kills: 1
[STATS] ====================================
[STATS] Kills: 5  |  Loots: 3
[STATS] Last loot: 'Red Rattlesnake' (12 sec ago)
[STATS] Corpses nearby: 0
[STATS] ====================================
```

---

## Notas Técnicas

### Como o Ataque Funciona

1. O controller lê HP do mob alvo via `ReadProcessMemory`
2. Se HP > 0: escreve endereço do mob no slot de alvo (`lp+0x290` e `lp+0x478`)
3. Envia tecla Enter para o jogo (simula o ataque)
4. Se HP ≤ 0: incrementa kill counter, procura próximo mob
5. Timer repete a cada 1000ms
6. **Safety**: Só ataca se jogo está em foreground

### Como o Follow Funciona

1. Lê posição do player alvo via `ENT_RAW_X` / `ENT_RAW_Y`
2. Calcula direção e distância
3. Clica no mapa na direção do alvo (proporcional: `dist * 0.6f`, máx 15.0f)
4. Timer repete a cada 800ms

### Como o Auto Loot Funciona

1. Percorre a árvore de entidades procurando mobs com HP < 0 (mortos)
2. Identifica corpos por HP negativo (ex: `0xFFFFFFE8` = -24)
3. Se longe (> 2 tiles): clica na direção do corpse para andar até ele
4. Se perto (≤ 2 tiles): clica no corpse + pressiona Enter para "Take All"
5. Timer repete a cada 1200ms
6. **Safety**: Só clica se jogo está em foreground e não minimizado

### Foreground Safety

Todos os timers verificam se o jogo está em foreground antes de agir:
- `GetForegroundWindow() == FindGameWindow()`
- `!IsIconic(w)` (não minimizado)
- Se o jogo não está em foco, o timer faz `break` (não clica)

### Filtros de Mob

O campo "Mob name" na aba Bot filtra quais mobs atacar:
- Vazio = ataca qualquer mob (exceto NPCs)
- "Rato" = ataca apenas mobs com "Rato" no nome
- Usa `wcsstr()` para substring match

---

## Pendente / Melhorias

| Item | Prioridade | Descrição |
|------|-----------|-----------|
| ~~Loot por tecla~~ | ~~Média~~ | **Feito** - Enter confirma Take All |
| ~~Foreground Safety~~ | ~~Alta~~ | **Feito** - Verifica janela antes de clicar |
| ~~Kill/Loot Counter~~ | ~~Média~~ | **Feito** - Stats no console e status bar |
| Follow direto por coordenada | Média | Usar WPM para mover em vez de cliques |
| Auto Heal | Baixa | Ler HP, comparar threshold, usar skill |
| UI Corpse List | Baixa | Mostrar lista de corpos na aba |
| Overlay | Baixa | Mostrar HP/distance na tela do jogo |


MEU PIX SE QUISER ME AJUDAR A COMPRAR LEITE PROS MEUS FILHOS 

""  CNPJ : 57944048000175  ""

RICHARD WILLIAN RAMOS - C6
