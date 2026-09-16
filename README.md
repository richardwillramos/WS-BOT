# Warspear Online Bot

Bot de automação para Warspear Online (cliente 32-bit). Funciona via DLL injetada no processo do jogo + controlador GUI externo.

---

## Arquitetura

```
┌─────────────────────────────┐         ┌─────────────────────────────┐
│  warspear-controller.exe    │         │  warspear-bot22.dll         │
│  (GUI externo)              │         │  (injetada no jogo)         │
│                             │         │                             │
│  Aba Connection: injetar    │──RPM───►│  Lê memória do jogo         │
│  Aba Bot: attack/follow     │         │  (offsets hardcoded)        │
│  Aba Players: lista         │         │                             │
│  Aba Mobs: lista            │◄─Shared─│  BotThread: loop infinito   │
│  Aba NPCs: lista            │  Memory │  - Follow: clica no alvo    │
│  Hotkeys F1/F3              │         │  - Attack: Tab+Click+Enter  │
│  Status bar                 │         │  - Log: bot_log.txt         │
└─────────────────────────────┘         └─────────────────────────────┘
         │                                        │
         │         Shared Memory                  │
         │    "Local\WarspearBotShared"           │
         │    struct BotCmd { ... }               │
         └────────────────────────────────────────┘
```

**Modo direto (sem DLL):** O controller também pode ler/escrever memória do jogo via `ReadProcessMemory`/`WriteProcessMemory` sem injetar a DLL. Nesse caso, usa timers internos (Timer 2 para ataque, Timer 3 para follow).

---

## Funcionalidades

### Implementadas

| Funcionalidade | DLL | Controller | Status |
|---------------|-----|------------|--------|
| Auto Attack (Tab + Click + Enter) | Sim | Sim | Funcionando |
| Follow Player (clicar no mapa) | Sim | Sim | Funcionando (oscilação pendente) |
| Injeção de DLL | - | Sim | Funcionando |
| Listagem de processos | - | Sim | Funcionando |
| Classificação de entidades | - | Sim | Funcionando |
| Seleção de alvo (duplo-clique) | - | Sim | Funcionando |
| Kill counter | Sim | - | Funcionando |

### Declaradas mas NÃO implementadas

| Funcionalidade | Evidência |
|---------------|-----------|
| Auto Heal | `healOn`/`healThreshold` no BotCmd, checkbox na UI, mas sem lógica |
| Overlay | `overlay.h` existe, mas nunca é incluído na DLL |
| Auto Collect | Mencionado no build.bat (F5), zero implementação |
| Protocolo completo | `BotSharedData` definido mas não utilizado; ambos usam `BotCmd` simplificado |

### Hotkeys

| Tecla | Ação |
|-------|------|
| **F1** | Toggle Auto Attack |
| **F3** | Toggle Follow Player |
| **Tab** | Seleciona mob (durante ataque) |
| **Enter** | Ataca alvo selecionado |

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
| `+0x10` | DWORD | Raw X (dividir por 65536) |
| `+0x14` | DWORD | Raw Y (dividir por 65536) |
| `+0x58` | DWORD | Ponteiro para nome |
| `+0x60` | DWORD | Tamanho do nome |
| `+0x64` | 32 wchar_t | String do nome (UTF-16LE) |
| `+0x10C` | DWORD | HP atual |
| `+0x110` | DWORD | HP máximo |
| `+0x114` | DWORD | Mana atual |
| `+0x118` | DWORD | Mana máximo |
| `+0x290` | DWORD | Alvo atual do jogador |
| `+0x478` | DWORD | Alvo secundário |

### VTables (Classificação)

| VTable | Tipo |
|--------|------|
| `0x00C80F9C` | Local Player |
| `0x00C8137C` | Humanoide (players, NPCs, mobs humanoides) |
| `0x00C81490` | Bestiário (mobs animais) |

---

## Estrutura do Projeto

```
warspear-bot/
├── dllmain.cpp                 ← DLL principal (bot injetado no jogo)
├── warspear-bot22.dll          ← DLL compilada (versão atual)
├── warspear-controller.exe     ← Controlador GUI compilado
├── bot_log.txt                 ← Log de execução
├── build.bat                   ← Compilação completa (DLL + Controller)
├── build-dll.bat               ← Compilação só da DLL
│
├── include/
│   ├── game_memory.h           ← Offsets, estruturas, helpers de leitura
│   ├── shared_protocol.h       ← Protocolo de memória compartilhada (NÃO UTILIZADO)
│   └── overlay.h               ← Overlay GDI (NÃO UTILIZADO)
│
└── controller/
    ├── main.cpp                ← Controlador GUI (767 linhas)
    └── warspear-controller.exe ← Compilado
```

---

## Build

### Pré-requisitos

- Visual Studio Build Tools (MSVC)
- `vcvars32.bat` deve estar acessível

### Compilar só a DLL

```bat
cd warspear-bot
build-dll.bat
```

### Compilar tudo (DLL + Controller)

```bat
cd warspear-bot
build-all.bat
```

### Compilação manual

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
cl /O2 /EHsc /MT /LD dllmain.cpp /Fewarspear-bot22.dll /link user32.lib
```

---

## Uso

1. Abrir Warspear Online
2. Abrir `warspear-controller.exe`
3. Aba **Connection**: selecionar o processo do jogo e clicar **CONNECT**
4. Clicar **Inject DLL** (selecionar `warspear-bot22.dll`)
5. Aba **Players**: duplo-clique num player para segui-lo
6. Aba **Mobs**: duplo-clique num mob para atacá-lo
7. Marcar checkbox ou usar F1/F3 para ativar

---

## Pontos a Corrigir

### Críticos

1. **Follow oscila quando chega perto** — O player chega perto do alvo e fica oscilando entre overshoot e undershoot. A fórmula `336 + dx * 20` mapeia tile→pixel mas o personagem overshoota. Precisa de deadzone maior ou abordagem diferente (ex: só clicar quando `dist2 > 16`).

2. **Nome da shared memory não bate** — `shared_protocol.h` usa `L"WarspearBotSharedMemory"` mas DLL e controller usam `L"Local\\WarspearBotShared"`. O header do protocolo é código morto.

3. **Heal não funciona** — Checkbox existe, campos existem no BotCmd, mas não tem lógica na DLL nem no controller.

4. **Listas de nomes divergentes** — `game_memory.h` (DLL) e `controller/main.cpp` têm listas de NPCs/mobs diferentes. A DLL inclui "Kpqbqtqk" como mob; o controller não. O controller adiciona "Norberto, o aougueiro" como NPC; a DLL não.

5. **Leitura de nome inconsistente** — `game_memory.h` lê nome inline em `objPtr + 0x64`, enquanto o controller lê via ponteiro em `objPtr + 0x058`. Duas interpretações diferentes do layout de memória.

### Melhorias

6. **Overlay inativo** — `overlay.h` existe mas nunca é incluído. Poderia mostrar HP, mana, distância do alvo, etc. na tela do jogo.

7. **Auto Collect** — Mencionado no build.bat mas nunca implementado. Poderia coletar drops automaticamente.

8. **F4/F5/F12 não implementados** — Hotkeys mencionadas no build.bat mas sem funcionalidade.

9. **Limpar DLLs antigas** — 22 versões de DLL compilada no diretório. Manter só a latest.

10. **Protocolo compartilhado não utilizado** — `BotSharedData` em `shared_protocol.h` é rico (listas de entidades, status, etc.) mas nunca é usado. Poderia substituir o `BotCmd` simplificado.

### Pontos de Atenção

11. **Anti-cheat** — O jogo pode ter detecção de injeção de DLL. Usar com cautela.

12. **Offset `0x00D387AC` hardcoded** — Se o jogo atualizar, os offsets mudam. Idealmente deveria ser configurável.

13. **Janela "Warspear Online" hardcoded** — `FindWindowA(NULL, "Warspear Online")` pode falhar se o título da janela mudar.

14. **Caminho do log hardcoded** — `bot_log.txt` é escrito em caminho absoluto fixo.

15. **Sem tratamento de erro** — Se o jogo crashar ou a janela fechar, o bot continua rodando.

---

## Próximos Passos Sugeridos

1. **Corrigir follow** — Testar com deadzone maior ou usar `WriteProcessMemory` para mover o personagem diretamente em vez de cliques.

2. **Implementar heal** — Ler HP do player, comparar com threshold, usar skill de heal quando necessário.

3. **Ativar overlay** — Incluir `overlay.h` na DLL e renderizar status na tela.

4. **Unificar listas de entidades** — Criar um arquivo compartilhado com nomes de players, mobs, NPCs.

5. **Tornar offsets configuráveis** — Mover hardcoded addresses para `config.json` ou similar.

6. **Adicionar Auto Collect** — Implementar coleta automática de drops.

7. **Limpar projeto** — Remover DLLs antigas,Unused code (`shared_protocol.h`, `overlay.h`), padronizar nomes.
