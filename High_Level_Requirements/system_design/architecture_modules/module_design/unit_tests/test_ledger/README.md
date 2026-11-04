# SGC Test Ledger

Living **cross-stack** test notebook for Ski Gate Chrono.

| File | Purpose |
|------|---------|
| **[TEST_LEDGER.md](TEST_LEDGER.md)** | Architecture map, **active case**, logs, defects, task queue |
| `history/` | Closed case snapshots |
| `cases/` | Optional long-form case files |
| `templates/case_template.md` | Copy for a new active case |

**Automated** smoke/core/full definitions remain in parent  
[`../TEST_CATALOG.md`](../TEST_CATALOG.md).

## Workflow

1. Open/update active case in `TEST_LEDGER.md` §2.  
2. Paste serial + Flutter logs in §3.  
3. Lead Systems Coordinator diagnoses → atomic tasks in §5.  
4. DeepSeek subagents implement; coordinator verifies.  
5. On close: copy case → `history/YYYY-MM-DD_<slug>.md`, reset §2.
