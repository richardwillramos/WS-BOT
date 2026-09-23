# Warspear Online Bot v5.1

Bot de automação para Warspear Online (cliente 32-bit, private server). Funciona via memória do processo do jogo (ReadProcessMemory/WriteProcessMemory) com arquitetura modular.

---

## Arquitetura

```
┌─────────────────────────────────┐
│  warspear-controller.exe        │
│  (GUI externa)                  │
│                                 │
│  Aba Config: TreeView (árvore)  │
│  Aba Quick: atalhos ON/OFF      │
│  Aba Connection: conectar/jogador│
│  Debug Console: logs em tempo real│
│  Status bar                     │
└──────────────┬──────────────────┘
               │ RPM / WPM
               ▼
┌─────────────────────────────────┐
│  warspear.exe (PID detectado)   │
│  32-bit, private server         │
└─────────────────────────────────┘
```

**Modo de operação:** O controller lê/escreve memória do jogo via `ReadProcessMemory`/`WriteProcessMemory` direto, sem necessidade de DLL injetada.

### Módulos (IModule)

| Módulo | Descrição |
|--------|-----------|
| **Targeter** | Seleciona mob com filtros (All/ByName/ByDistance), mantém alvo |
| **Attacker** | Ataca via HandleMoveOrAction + detecção de espada + Enter remoto |
| **Healer** | Cura player selecionado via target (timer configurável) |
| **Follower** | Segue um player específico com walk flag + Enter local |
| **Looter** | Coleta corpos: caminha até corpse + HandleMoveOrAction + Enter |
| **Extra** | Anti-AFK, auto-revive, auto-sell, auto-repair |

---

## Debug Console

O bot possui um console de debug que mostra informações em tempo real sobre todas as ações:

| Tag | Informação Exibida |
|-----|-------------------|
| `[TARGETER]` | Mob selecionado, endereço, distância |
| `[ATTACK]` | Coordenadas do mob, tile, detecção da espada |
| `[FOLLOW]` | Alvo, distância, coordenadas |
| `[LOOT]` | Corpse, distância, walk/loot, contador de loots |
| `[HEAL]` | Endereço do alvo, tecla usada |
| `[STATS]` | A cada 10s: posição, entidades, módulos ativos |

---

## Offsets de Memória

### Cadeia de Ponteiros Principal

```
warspear.exe + 0x00D387AC  →  sysInstance (System Instance)
sysInstance  + 0x14        →  GM (GameManager)
GM + 0x40                  →  player (LocalPlayer)
```

### Offsets de Entidade

| Offset | Tamanho | Descrição |
|--------|---------|-----------|
| `+0x00` | DWORD | VTable (identificador de classe) |
| `+0x10` | WORD | Raw X (ler como short, valor direto) |
| `+0x14` | WORD | Raw Y (ler como short, valor direto) |
| `+0x58` | DWORD | Ponteiro para nome (UTF-16LE) |
| `+0x060` | DWORD | Tamanho do nome |
| `+0x090` | DWORD | Tipo (1=player, 2=outro) |
| `+0x10C` | DWORD | HP atual |
| `+0x110` | DWORD | HP máximo |
| `+0x114` | DWORD | Mana atual |
| `+0x118` | DWORD | Mana máximo |
| `+0x290` | DWORD | Alvo atual do jogador |
| `+0x2E0` | DWORD | Nível do mob/jogador |
| `+0x3ED` | BYTE  | Class ID (ver tabela abaixo) |
| `+0x478` | DWORD | Alvo secundário |

### Class IDs

| ID | Classe |
|----|--------|
| 0 | Undefined |
| 1 | Paladin |
| 2 | Priest |
| 3 | Mage |
| 4 | Barbarian |
| 5 | Rogue |
| 6 | Shaman |
| 7 | Bladedancer |
| 8 | Ranger |
| 9 | Druid |
| 10 | Deathknight |
| 11 | Necromancer |
| 12 | Warlock |
| 13 | Seeker |
| 14 | Hunter |
| 15 | Warden |
| 16 | Charmer |
| 17 | Templar |
| 18 | Chieftain |
| 19 | Beastmaster |
| 20 | Reaper |

### Cursor / Ações

O cursor do jogo é acessível via `GM + 0x123C`:

| Offset | Tamanho | Descrição |
|--------|---------|-----------|
| `+0x08` | WORD | Cursor X (tiles) |
| `+0x0A` | WORD | Cursor Y (tiles) |
| `+0x10` | DWORD | Raw X (escrita para mover cursor) |
| `+0x14` | DWORD | Raw Y (escrita para mover cursor) |
| `+0x7C` | DWORD | Walk flag / Cursor action flag |

**Walk flag:** Escrever `0x10` em `cursor+0x7C` faz o personagem andar até a posição do cursor.

**Cursor action flags:**

| Valor | Ação |
|-------|------|
| `8` | ATTACK (cursor sobre mob hostil) |
| `13` | MOVE (cursor em posição válida) |
| `15` | NONE (cursor em posição inválida) |

**Como funciona o cursor:**
1. Escrever tile X/Y em `cursor+0x08` e `cursor+0x0A` (WORD)
2. Escrever raw X/Y em `cursor+0x10` e `cursor+0x14` (DWORD = tile * 0x180000)
3. Escrever walk flag `0x10` em `cursor+0x7C` para movimentar
4. Chamar `HandleMoveOrAction` via `CreateRemoteThread` para o jogo processar
5. Verificar `cursor_action == 8` (espada) antes de confirmar ataque

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
| `0x00C81490` | Mob (bestiário) - inclui corpses (HP < 0) |
| `0x00C4FC5C` | Objeto/Spawn (drops, objetos interativos) |

### Coordenadas do Mundo

- Zona size: 28 tiles (0-27)
- Conversão tile→raw (cursor): `rawX = tileX * 0x180000`
- Entity raw: WORD (2 bytes), ler como `short`, valor direto (sem divisão)

### Funções Relevantes

| Endereço | Função | Descrição |
|----------|--------|-----------|
| `0x00A3F480` | `HandleMoveOrAction` | Processa movimento/ação do cursor (__thiscall localPlayer, 0) |
| `0x00A3E0F0` | `HandleSkillOrUse` | Processa skill/uso (__thiscall localPlayer, entityPtr) |

---

## Como o Ataque Funciona

1. **Targeter** seleciona mob mais próximo com HP > 0 (com filtros: All/ByName/ByDistance)
2. **Attacker** escreve coordenadas do mob no cursor via `WriteProcessMemory`
3. Chama `HandleMoveOrAction` via `CreateRemoteThread` — o jogo processa a posição do cursor
4. Lê `cursor_action` em `cursor_ptr + 0x7C`:
   - Se `== 8` (ATTACK/espada detectada) → seta alvo + envia Enter → ataque
   - Se `!= 8` → continua tentando (retries)
5. **Looter** caminha até corpse (walk flag 0x10) + `HandleMoveOrAction` + Enter para coletar

### Por que não PostMessage?

O Warspear Online usa DirectInput/raw input — `PostMessage` com `WM_KEYDOWN` não funciona. A solução é usar `CreateRemoteThread` para chamar funções do próprio jogo (`HandleMoveOrAction`) e `AttachThreadInput` + `keybd_event(VK_RETURN)` para gerar input real.

---

## Estrutura do Projeto

```
WS-BOT/
├── controller/
│   └── main.cpp                ← Controlador GUI (TreeView UI, 6 módulos, debug console)
│
├── include/
│   ├── IModule.h               ← Interface dos módulos + GameContext
│   ├── ModuleManager.h         ← Registro, persistência de config
│   └── game_memory.h           ← Offsets, estruturas, cursor struct
│
├── modules/
│   ├── Targeter.h              ← Seleção de alvo (All/ByName/ByDistance)
│   ├── Attacker.h              ← Ataque via HandleMoveOrAction + sword detection
│   ├── Healer.h                ← Cura player selecionado (target + timer)
│   ├── Looter.h                ← Auto-loot (walk + HandleMoveOrAction)
│   ├── Follower.h              ← Seguir player (walk flag + Enter local)
│   └── Extra.h                 ← Anti-AFK, auto-revive, auto-sell, auto-repair
│
├── build-controller-local.bat  ← Script de compilação do controller
└── README.md
```

---

## Build

### Pré-requisitos

- Visual Studio 2026 Build Tools (MSVC x86)
- `vcvars32.bat` em `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\`

### Compilar o Controller

```bat
cd WS-BOT
build-controller-local.bat
```

### Flags de Compilação Atuais

```
/nologo /O2 /EHsc /MTd /GS-
```

> **Nota:** Usa debug CRT (`/MTd`) porque release CRT (`/MT`) causa crash 0xC0000409 (fastfail). `/GS-` desabilita buffer overrun detection.

---

## Uso

1. Abrir Warspear Online (deixa o jogo carregado)
2. Abrir `warspear-controller.exe`
3. Aba **Connection**: selecionar `warspear.exe` na lista e clicar **CONNECT**
4. Aba **Config**: expandir módulos (+) e ativar os desejados
5. **Healer**: clicar em "Target" para selecionar o player a curar
6. **Follower**: clicar em "Target" para selecionar o player a seguir
7. **Targeter**: selecionar modo de filtro (All/ByName/ByDistance)
8. Aba **Quick**: atalhos ON/OFF para Attack, Heal, Follow
9. O bot começa a trabalhar automaticamente

### Atalhos de Teclado

| Tecla | Ação |
|-------|------|
| F1 | Toggle Attack |
| F2 | STOP ALL (panic) |
| F3 | Toggle Follow |
| F4 | Toggle Loot |
| F5 | Scale + |
| F6 | Scale - |

---

## Notas Técnicas

### Foreground Safety

Todos os módulos verificam se o jogo está em foreground antes de agir:
- `GetForegroundWindow() == ctx.gameWindow`
- `!IsIconic(gw)` (não minimizado)

### Multi-Instance

O bot suporta múltiplas instâncias do Warspear Online. Cada controller conecta a um PID diferente.

### UI (TreeView)

A UI principal usa um TreeView (árvore hierárquica) com 6 módulos:
- **Targeter**: Enabled, Filter Mode (All/ByName/ByDistance), Mob Name, Max Distance, Retarget, Whitelist, Blacklist
- **Attacker**: Status, Cooldown, Skills
- **Healer**: Status, Target (seletor de player), Cooldown, Heal key, Min HP filter (ON/OFF), Min HP%
- **Follower**: Status, Target (seletor de player), Distance, Max distance
- **Looter**: Status, Radius, Cooldown
- **Extra**: Anti AFK, Auto Revive, Auto Sell, Auto Repair

Cada módulo é um nó pai que expande/recolhe com "+". Cliques nos filhos alternam valores ou abrem input dialogs.

### Targeter - Filtros

- **All**: Ataca qualquer mob (usa whitelist/blacklist)
- **By Name**: Ataca apenas mobs com nome exato (evita variantes mais fortes)
- **By Distance**: Ataca mob mais próximo dentro do maxDistance

### Healer - Funcionamento

- **Target**: seleciona um player da lista de nearby
- **Cooldown**: tempo em ms entre cada cura
- **Heal Key**: tecla da skill de cura (1-9)
- **Min HP filter OFF**: cura no timer sem checar HP
- **Min HP filter ON**: só cura se HP do target abaixo do %

### DLL (Legacy)

`warspear-bot23.dll` é uma versão legada que usava arrow-key navigation via shared memory. O controller atual não precisa dela — usa `WriteProcessMemory` + `CreateRemoteThread` diretamente.
