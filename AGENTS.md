<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **Soul-Knight-ai** (9088 symbols, 26831 relationships, 61 execution flows). Use the GitNexus MCP tools to understand code, assess impact, and navigate safely.

> If any GitNexus tool warns the index is stale, run `npx gitnexus analyze` in terminal first.

## Always Do

- **MUST run impact analysis before editing any symbol.** Before modifying a function, class, or method, run `gitnexus_impact({target: "symbolName", direction: "upstream"})` and report the blast radius (direct callers, affected processes, risk level) to the user.
- **MUST run `gitnexus_detect_changes()` before committing** to verify your changes only affect expected symbols and execution flows.
- **MUST warn the user** if impact analysis returns HIGH or CRITICAL risk before proceeding with edits.
- When exploring unfamiliar code, use `gitnexus_query({query: "concept"})` to find execution flows instead of grepping. It returns process-grouped results ranked by relevance.
- When you need full context on a specific symbol — callers, callees, which execution flows it participates in — use `gitnexus_context({name: "symbolName"})`.

## Never Do

- NEVER edit a function, class, or method without first running `gitnexus_impact` on it.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis.
- NEVER rename symbols with find-and-replace — use `gitnexus_rename` which understands the call graph.
- NEVER commit changes without running `gitnexus_detect_changes()` to check affected scope.

## Resources

| Resource | Use for |
|----------|---------|
| `gitnexus://repo/Soul-Knight-ai/context` | Codebase overview, check index freshness |
| `gitnexus://repo/Soul-Knight-ai/clusters` | All functional areas |
| `gitnexus://repo/Soul-Knight-ai/processes` | All execution flows |
| `gitnexus://repo/Soul-Knight-ai/process/{name}` | Step-by-step execution trace |

## CLI

| Task | Read this skill file |
|------|---------------------|
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus/gitnexus-cli/SKILL.md` |
| Work in the Combat area (1058 symbols) | `.claude/skills/generated/combat/SKILL.md` |
| Work in the Util area (111 symbols) | `.claude/skills/generated/util/SKILL.md` |
| Work in the Tools area (107 symbols) | `.claude/skills/generated/tools/SKILL.md` |
| Work in the World area (71 symbols) | `.claude/skills/generated/world/SKILL.md` |
| Work in the Test area (68 symbols) | `.claude/skills/generated/test/SKILL.md` |
| Work in the Sim area (52 symbols) | `.claude/skills/generated/sim/SKILL.md` |
| Work in the Data area (37 symbols) | `.claude/skills/generated/data/SKILL.md` |
| Work in the Ui area (29 symbols) | `.claude/skills/generated/ui/SKILL.md` |
| Work in the Entities area (28 symbols) | `.claude/skills/generated/entities/SKILL.md` |
| Work in the Scenes area (19 symbols) | `.claude/skills/generated/scenes/SKILL.md` |
| Work in the Game area (15 symbols) | `.claude/skills/generated/game/SKILL.md` |
| Work in the Physics area (10 symbols) | `.claude/skills/generated/physics/SKILL.md` |
| Work in the Cluster_33 area (8 symbols) | `.claude/skills/generated/cluster-33/SKILL.md` |
| Work in the Doxygen-awesome area (7 symbols) | `.claude/skills/generated/doxygen-awesome/SKILL.md` |
| Work in the Interactive area (7 symbols) | `.claude/skills/generated/interactive/SKILL.md` |
| Work in the Cluster_34 area (4 symbols) | `.claude/skills/generated/cluster-34/SKILL.md` |
| Work in the Cluster_201 area (3 symbols) | `.claude/skills/generated/cluster-201/SKILL.md` |

<!-- gitnexus:end -->
