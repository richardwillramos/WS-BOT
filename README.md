# Warspear Online Bot v5.4

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
│  Status bar (Dev By Richard Willian)│
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
| **Targeter** | Seleciona mob com filtros (All/ByName/ByDistance/Damaged), mantém alvo |
| **Attacker** | Ataca: cursor no mob, flag 8 + Enter; **skills** da UI/INI disparam key+Enter no ciclo |
| **Healer** | Cura o alvo e a si mesmo; filtro por **cooldown** ou **HP%** (prioridade máxima) |
| **Follower** | Segue um player: escreve cursor no tile dele + Enter |
| **Looter** | Coleta corpos da tree e mobs mortos (cursor + Enter); Walk Radius e Max Distance configuráveis |
| **Extra** | Anti-AFK, auto-revive, auto-sell, auto-repair, **auto-buff** (tecla + cursor em si + Enter) |
| **Dungeon** | Supervisa uma dungeon: mata waves → portal → boss/loot → baú → saída (automação por fases) |

**Ordem de prioridade do tick:** `Healer → Dungeon → Follower → Looter → Targeter → Attacker → Extra`.
Quando o HP do próprio char ou do alvo está abaixo do limite, `holdCombat`
pausa Attacker e Looter — **cura > loot > ataque** (teto de 15 s, nunca trava o bot).
Durante as fases de interação da dungeon, `dungeonBusy` faz Targeter, Attacker,
Follower e Looter cederm o tick; nas waves sem drop, `dungeonNoLoot` segura o Looter.

---

## Debug Console

O bot possui um console de debug que mostra informações em tempo real sobre todas as ações:

| Tag | Informação Exibida |
|-----|-------------------|
| `[TARGETER]` | Mob selecionado, endereço, distância |
| `[ATTACK]` | Coordenadas do mob, tile, detecção da espada |
| `[FOLLOW]` | Alvo, distância, coordenadas |
| `[LOOT]` | Corpse, distância, walk/loot, contador de loots |
| `[HEAL]` | Self/target com HP%, tiles, tecla; logs de hold/release da prioridade |
| `[DUNGEON]` | Fase da run, walks, Enters (portal/baú/saída), esperas por entidade |
| `[STATS]` | A cada 10s: posição, entidades, módulos ativos |

---

## Offsets de Memória

### Cadeia de Ponteiros Principal

```
warspear.exe + 0x00D8F98C  →  sysInstance (System Instance)
sysInstance  + 0x14        →  GM (GameManager)
GM + 0x40                  →  player (LocalPlayer)
GM + 0x3C                  →  entity tree header (+0x00 = root)
GM + 0x1244                →  cursor struct
```

> **Atualizado após o patch do jogo.** O ponteiro antigo era `0x00D387AC`.
> Se o Connect falhar com *"Cannot read game memory"*, confirme estes ponteiros.

### Offsets de Entidade

| Offset | Tamanho | Descrição |
|--------|---------|-----------|
| `+0x00` | DWORD | VTable (identificador de classe) |
| `+0x10` | DWORD | X do mundo (16.16 → valor/65536; tiles = /24) |
| `+0x14` | DWORD | Y do mundo (16.16 → valor/65536; tiles = /24) |
| `+0x58` | DWORD | Ponteiro para nome (UTF-16LE) |
| `+0x60` | DWORD | Tamanho do nome |
| `+0x90` | DWORD | Tipo (1=player, 2=outro — não separa NPC de mob hostil) |
| `+0x110` | DWORD | HP atual (morto = valor negativo, ex. -79) |
| `+0x114` | DWORD | HP máximo |
| `+0x118` | DWORD | Mana atual |
| `+0x11C` | DWORD | Mana máximo |
| `+0x294` | DWORD | Alvo atual (LEITURA ok; **NUNCA escrever** — ver nota) |
| `+0x2E4` | BYTE  | Nível do mob/jogador |
| `+0x3F9` | BYTE  | Class ID (ver tabela abaixo) |
| `+0x484` | DWORD | Alvo secundário (**NUNCA escrever** — ver nota) |

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

O cursor do jogo é acessível via `GM + 0x1244` (antes `0x123C`):

| Offset | Tamanho | Descrição |
|--------|---------|-----------|
| `+0x08` | WORD | Cursor X (tiles) |
| `+0x0A` | WORD | Cursor Y (tiles) |
| `+0x10` | DWORD | Raw X (escrita para mover cursor) |
| `+0x14` | DWORD | Raw Y (escrita para mover cursor) |
| `+0x7C` | DWORD | Walk flag / Cursor action flag |

**Andar:** escrever as coordenadas **não** move o personagem — só reposiciona o
cursor e o jogo recalcula a flag. Para andar, escreva o cursor no tile e envie **Enter**
(é o que Follower e Looter fazem). Escrever `0x10` em `+0x7C` é inútil: o jogo
sobrescreve a flag em poucos ms.

**Cursor action flags:**

| Valor | Ação |
|-------|------|
| `8` | ATTACK (cursor sobre mob hostil) |
| `13` | MOVE (cursor em posição válida) |
| `15` | NONE (cursor em posição inválida) |

**Como funciona o cursor:**
1. Escrever tile X/Y em `cursor+0x08` e `cursor+0x0A` (WORD)
2. Escrever raw X/Y em `cursor+0x10` e `cursor+0x14` (DWORD = tile * 0x180000)
3. **Não é preciso chamar função nenhuma** — o jogo recalcula `cursor+0x7C` sozinho
   em poucos ms (< 50 ms) a partir das coordenadas que você escreveu
4. Verificar `cursor_action == 8` (espada) antes de confirmar ataque

> `HandleMoveOrAction` (`0x00A3F480`) **foi desabilitado**: o endereço morreu no patch
> e chamá-lo quebrava o bot. Chamá-lo é hoje um no-op em `RemoteHandleMoveOrAction`.

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
| `0x00CD12D0` | Local Player |
| `0x00CD16BC` | Humanoide (NPC, jogadores, mobs humanoides) |
| `0x00CD17B4` | Bestiário (Aranha, Javali, etc.) |
| `0x00000000` | Corpo — desconhecido; cadáveres são detectados por `hp < 0` |

Vtables verificadas ao vivo em `warspear.exe` após o patch.

### Lista de NPCs editável

NPCs são separados de mobs **pelo nome**, lista em `config\npc_names.json`
(re-lida a cada Connect). Nome com `*` no final = prefixo:

```json
{
  "_ajuda": "Nomes normais = igualdade exata. 'Vendedor*' = prefixo.",
  "npcs": ["Mestre de Armas", "Guarda*", "Aranha feroz"]
}
```

Mob hostil listado aqui **não será atacado**. Mob hostil ausente daqui vira
"alvo" e, se o cursor não mostrar a espada, o Attacker desiste em ~1.5 s e
**estaciona o alvo por 60 s** (evita o travamento eterno antigo).

### Coordenadas do Mundo

- Zona size: 28 tiles (0-27)
- Conversão tile→raw (cursor): `rawX = tileX * 0x180000`
- Entity raw: WORD (2 bytes), ler como `short`, valor direto (sem divisão)

### Funções Relevantes

| Endereço | Função | Status |
|----------|--------|--------|
| `0x00000000` | `HandleMoveOrAction` (antes `0x00A3F480`) | **MORTO no patch — no-op** |
| `0x00000000` | `HandleSkillOrUse` (antes `0x00A3E0F0`) | **MORTO no patch — não usado** |

Não há mais chamada ao jogo para ações: tudo passa por escrita no cursor +
tecla real (Enter).

---

## Como o Ataque Funciona

1. **Targeter** seleciona mob mais próximo com HP > 0 (filtros: All/ByName/ByDistance)
2. **Attacker** escreve o tile do mob no cursor (`+0x08/+0x0A` WORD, `+0x10/+0x14` DWORD)
3. Espera 200 ms e lê `cursor+0x7C` — o jogo recalcula sozinho:
   - `== 8` (espada) → envia **Enter** (via `AttachThreadInput` + `keybd_event`,
     exige janela do jogo em primeiro plano)
   - `!= 8` → tenta de novo; após **5 falhas** derruba o alvo e o estaciona 60 s

> **Crítico (verificado em 24/09/2026):** o ataque **NÃO** deve escrever o alvo em
> `lp+0x294`/`lp+0x484` antes do Enter — isso **bloqueia o golpe** (flag 8 + Enter
> sem escrita drenou 278→159 num golpe; com escrita, HP não muda). O Enter sozinho
> com cursor no mob (flag 8) e personagem adjacente funciona.
4. **Looter** detecta corpses por `hp < 0`, escreve cursor no tile do loot e envia Enter

### Por que não PostMessage?

O Warspear Online usa DirectInput/raw input — `PostMessage` com `WM_KEYDOWN` não funciona.
A solução é `AttachThreadInput` + `keybd_event(VK_RETURN)` para gerar input real.
(O cursor não precisa de input: basta escrever as coordenadas em memória.)

---

## Estrutura do Projeto

```
WS-BOT/
├── controller/
│   └── main.cpp                ← Controlador GUI (TreeView UI, 7 módulos, debug console)
│
├── include/
│   ├── IModule.h               ← Interface dos módulos + GameContext
│   ├── ModuleManager.h         ← Registro, persistência de config
│   └── game_memory.h           ← Offsets, estruturas, cursor struct
│
├── modules/
│   ├── Targeter.h              ← Seleção de alvo + estaciona alvos sem espada (60s)
│   ├── Attacker.h              ← Ataque: cursor → flag 8 → alvo+Enter; cede a holdCombat
│   ├── Healer.h                ← Cura (Heal Mode: cooldown ou HP%, self-heal, NeedsHeal)
│   ├── Looter.h                ← Auto-loot (corpses da tree + mobs mortos, Max Distance)
│   ├── Follower.h              ← Seguir player (cursor no tile + Enter)
│   ├── Extra.h                 ← Anti-AFK, auto-revive, auto-sell, auto-repair, auto-buff
│   └── Dungeon.h               ← Supervisor de dungeon (WAVE1→PORTAL→WAVE2→BOSS_LOOT→CHEST→EXIT)
│
├── config/
│   ├── *.ini                   ← Persistência dos módulos (ModuleManager)
│   ├── npc_names.json          ← Lista editável de NPCs (re-lida no Connect)
│   └── boss_names.json         ← Nomes dos bosses (opcional; vazio = sem checagem de boss)
│
├── controller/                 ← main.cpp + config/ (o exe carrega config do próprio diretório)
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

> Existem dois binários (`warspear-controller.exe` na raiz e
> `controller\warspear-controller.exe`) — **mesmo fonte**; reconstrua os dois
> após qualquer alteração para não usar um exe desatualizado.

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
5. **Healer**: clicar em "Target" para selecionar o player a curar e escolher o
   **Heal Mode** (`Every cooldown` ou `When HP% below`)
6. **Follower**: clicar em "Target" para selecionar o player a seguir
7. **Targeter**: selecionar modo de filtro (All/ByName/ByDistance)
8. **Dungeon** (opcional): ao entrar na dungeon, ativar `Status` do módulo
   Dungeon — com Targeter/Attacker/Looter ligados, ele sozinho limpa as waves,
   usa o portal, mata o boss, coleta o baú e sai; desativa sozinho ao fim
9. Aba **Quick**: atalhos ON/OFF para Attack, Heal, Follow
10. O bot começa a trabalhar automaticamente

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
- **Targeter**: Enabled, Filter Mode (All/ByName/ByDistance/Damaged), Mob Name, Max Distance, Retarget, Whitelist, Blacklist
- **Attacker**: Status, Cooldown, Skills
- **Healer**: Status, Target (seletor de player), Cooldown, Heal key, **Heal Mode** (Every cooldown / HP% below), Min HP%, Self Heal, Self HP%, Self Key
- **Follower**: Status, Target (seletor de player), Distance, Max distance
- **Looter**: Status, Radius, Cooldown, Max Distance
- **Extra**: Anti AFK, Auto Revive, Auto Sell, Auto Repair

Cada módulo é um nó pai que expande/recolhe com "+". Cliques nos filhos alternam valores ou abrem input dialogs.

### Targeter - Filtros

- **All**: Ataca qualquer mob (usa whitelist/blacklist)
- **By Name**: Ataca apenas mobs com nome exato (evita variantes mais fortes)
- **By Distance**: Ataca mob mais próximo dentro do maxDistance
- **Damaged (assist)**: Ataca só mobs que **já apanharam** (`hp < maxHp` — seu ou
  da party) e prioriza o de **menor HP%**; combinado com *Retarget*, foca o alvo
  que você mesmo magoou (ex.: acertou com uma skill de área)

### Healer - Funcionamento

- **Target**: seleciona um player da lista de nearby
- **Heal Mode** (combo, mesmo padrão do filtro do Targeter):
  - **Every cooldown (N sec)**: cura a cada `Cooldown` ms, ignorando HP
  - **When HP% below**: só cura se HP do alvo (ou HP próprio) ≤ o limite —
    mantém o HP **sempre acima** da % definida
- **Cooldown (ms)**: intervalo entre curas (padrão `2000`; os INIs antigos
  traziam `10000`, o que atrasava demais a cura)
- **Heal Key**: tecla da skill de cura (1-9)
- **Self Heal**: cura própria automática por HP% (Self HP% + Self Key + Enter)
  com **cooldown próprio** — uma self-heal nunca atrasa a cura do alvo
- Alvo morto (`hp <= 0`) é ignorado; a self-heal roda **primeiro** dentro do tick
- A checagem de necessidade (`NeedsHeal`) acontece **antes** do cooldown
- **Prioridade**: `NeedsHeal` sinaliza `holdCombat` no GameContext → Attacker e
  Looter cedem o tick (cura > loot > ataque), com teto de 15 s. No modo
  *Every cooldown* a cura nunca segura o combate (não é urgente)

### Prioridade: Cura > Loot > Ataque

A ordem de `modMgr.Add()` define a ordem do tick (timer de 200 ms):

```
Healer → Dungeon → Follower → Looter → Targeter → Attacker → Extra
```

Se o HP do próprio char ou do alvo estiver abaixo do limite
(`Healer::NeedsHeal`), o `GameContext` sai com `holdCombat = true`:

- **Attacker** e **Looter** ignoram o tick até o HP subir
- Teto de **15 s**: se a cura não surtir efeito (alcance/mana/alvo fora),
  o combate é liberado e rearma assim que o HP sobe de novo — nunca trava o bot

### Dungeon - Automação

O módulo **Dungeon** supervise uma run completa. Início manual: ative `Status`
ao entrar na dungeon (Targeter/Attacker/Looter continuam ligados — quem cede
o tick quando o dungeon manda é eles):

```
WAVE1 (matar) → PORTAL1 (andar + Enter no portal) → WAVE2 (matar, boss por último)
→ BOSS_LOOT (looter coleta o drop) → CHEST (baú: abre + coleta) → EXIT (saída + confirma)
→ DONE (auto-disable)
```

- **Fases de interação** (`PORTAL1`, `CHEST`, `EXIT`) setam `dungeonBusy`:
  Targeter, Attacker, Follower e Looter ficam em espera; o Dungeon anda até
  ficar perto (Walk radius), escreve o cursor na entidade e aperta **Enter**,
  repetindo com backoff. Os mobs das waves setam `dungeonNoLoot` se
  `LootInWaves=0` (waves não dropam loot)
- **Fim de wave**: nenhum mob vivo por 3 s seguidos. Em WAVE2, se houver
  lista de bosses, exige também ver o boss (ou 120 s de fallback)
- **Tree**: `Status` (liga/desliga), `Phase` (fase atual), `Portal` / `Chest` /
  `Exit` (nomes — clique abre diálogo de texto), `Walk radius`, `Max distance`,
  `Loot in waves`, `Loot tries` (Enter`s no baú)
- **INI** (`config\Dungeon.ini`): `Portal1Name`, `ChestName`, `ExitName`,
  `WalkRadius`, `MaxDist`, `CollectTries`, `LootInWaves`, `AutoDisable`
- **`config\boss_names.json`**: nomes dos bosses (`*` no final = prefixo);
  lista vazia = qualquer "tudo morto" encerra a wave (re-lida no Connect)
- Logs `[DUNGEON]` no console: fase, walks, Enters, esperas
- **A validar ao vivo**: nome real do portal/baú/saída, se Enter bate no
  portal, o diálogo "Tem certeza..." (Enter confirma 2×) e a coleta do baú —
  os valores default são chute e se corrigem pelo diálogo na tree

### Attacker - Skills

- Clique em **Skills** na tree → diálogo `1, 3` (teclas separadas por vírgula)
- No ciclo de ataque, quando a espada (flag 8) aparece, se alguma skill está
  fora de cooldown ela dispara **key + Enter** antes do golpe normal
- Cooldown por skill: `SkillNCooldown` no INI (padrão 1500 ms); ordem por
  `SkillNPriority` (menor primeiro)
- INI equivalente: `Skill1Name/Key/Cooldown/Priority/Enabled` ... `Skill6` —
  os exemplos já vêm em `config\Attacker.ini` (teclas 1 e 3)

### Extra - Auto Buff

- Painel do Extra: **Auto Buff** + `Buff key` (padrão `5`) + `Buff cooldown`
  (padrão 10000 ms)
- No tick: cursor no **próprio char** + tecla + Enter (mesmo padrão do
  self-heal do Healer); pula durante `dungeonBusy` para não bagunçar diálogo
- INI: `BuffEnabled`, `BuffKey`, `BuffCooldown` em `config\Extra.ini`

### DLL (Legacy)

`warspear-bot23.dll` é uma versão legada que usava arrow-key navigation via shared memory. O controller atual não precisa dela — tudo roda via `ReadProcessMemory`/`WriteProcessMemory` + `keybd_event` local (`AttachThreadInput`). A DLL continua sendo injetável pelos botões do controller, mas o fluxo de ataque/loot/follow vive no controller.
