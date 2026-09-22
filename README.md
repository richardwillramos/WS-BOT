# Warspear Online Bot v5.0

Bot de automação para Warspear Online (cliente 32-bit, private server). Funciona via memória do processo do jogo (ReadProcessMemory/WriteProcessMemory) com arquitetura modular.

---

## Arquitetura

```
┌─────────────────────────────────┐
│  warspear-controller.exe        │
│  (GUI externa)                  │
│                                 │
│  Aba Config: módulos accordion  │
│  Aba Quick: atalhos ON/OFF      │
│  Aba Connection: conectar/jogador│
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
| **Targeter** | Seleciona mob mais próximo, mantém alvo |
| **Attacker** | Ataca alvo via setas do cursor + Enter |
| **Healer** | Cura o jogador quando HP cai abaixo do threshold |
| **PartyHealer** | Cura membros do grupo |
| **Looter** | Coleta corpos de mobs mortos |
| **Follower** | Segue o jogador mais próximo |
| **Extra** | Anti-AFK, auto-revive, auto-sell, auto-repair |

---

## Offsets de Memória

### Cadeia de Ponteiros Principal

```
warspear.exe + 0x009387AC  →  sysInstance (System Instance)
sysInstance  + 0x14        →  GM (GameManager)
GM + 0x40                  →  player (LocalPlayer)
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

O cursor do jogo é acessível via `player + 0x123C`:

| Offset | Tamanho | Descrição |
|--------|---------|-----------|
| `+0x00` | DWORD | Cursor X (tiles) |
| `+0x04` | DWORD | Cursor Y (tiles) |
| `+0x7C` | DWORD | Cursor action flag |

**Cursor action flags:**

| Valor | Ação |
|-------|------|
| `8` | ATTACK (cursor sobre mob hostil) |
| `13` | MOVE (cursor em posição válida) |
| `15` | NONE (cursor em posição inválida) |

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
- Conversão raw→tile: `(raw / 65536) % 28`

### Strings Chave no Binário

| Endereço | String |
|----------|--------|
| `0x00c77bf0` | `"LogicLoot"` |
| `0x00c77bd8` | `"LogicLoot::sel_selector"` |
| `0x00c59bb0` | `"icon_target_corpse"` |
| `0x00c5ccd8` | `"player_corpse_u"` |
| `0x00c59a5c` | `"icon_loot_all"` |

### Funções Relevantes (Ghidra)

| Endereço | Função | Descrição |
|----------|--------|-----------|
| `0x00963AA0` | `FUN_00963aa0` | Loot selection handler |
| `0x00963C80` | `FUN_00963c80` | Loot initialization |
| `0x00A67580` | `FUN_00a67580` | Server object spawn handler |

---

## Como o Ataque Funciona

1. Targeter seleciona mob mais próximo com HP > 0
2. Attacker usa setas do teclado para mover o cursor até a posição do mob
3. Lê `cursor_action` em `cursor_ptr + 0x7C`
4. Se `cursor_action == 8` (ATTACK) → pressiona Enter
5. Se `cursor_action != 8` → move cursor com setas

---

## Estrutura do Projeto

```
WS-BOT/
├── controller/
│   └── main.cpp                ← Controlador GUI (accordion UI)
│
├── include/
│   ├── IModule.h               ← Interface dos módulos + GameContext
│   ├── ModuleManager.h         ← Registro, persistência de config
│   └── game_memory.h           ← Offsets, estruturas
│
├── modules/
│   ├── Targeter.h              ← Seleção de alvo
│   ├── Attacker.h              ← Ataque via setas + Enter
│   ├── Healer.h                ← Auto-cura
│   ├── PartyHealer.h           ← Cura de grupo
│   ├── Looter.h                ← Auto-loot
│   ├── Follower.h              ← Seguir jogador
│   └── Extra.h                 ← Anti-AFK, auto-revive, etc.
│
├── dllmain.cpp                 ← DLL (setas do cursor via memória compartilhada)
├── build-controller-local.bat  ← Script de compilação do controller
├── build-dll.bat               ← Script de compilação da DLL
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
4. Aba **Config**: expandir módulos e ativar os desejados
5. Aba **Quick**: atalhos ON/OFF para Attack, Heal, Follow
6. O bot começa a trabalhar automaticamente

---

## Notas Técnicas

### Foreground Safety

Todos os timers verificam se o jogo está em foreground antes de agir:
- `GetForegroundWindow() == FindGameWindow()`
- `!IsIconic(w)` (não minimizado)

### Multi-Instance

O bot suporta múltiplas instâncias do Warspear Online. Cada controller conecta a um PID diferente.

### DLL (Opcional)

`warspear-bot23.dll` pode ser injetada para ações adicionais via memória compartilhada (`Local\WarspearBotShared`). Usa arrow-key navigation via WriteProcessMemory no programa do jogo.
