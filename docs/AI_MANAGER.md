# ApelsinBot AI Manager — first implementation

## Scope
The first increment adds a deterministic, observe-only decision core. It does not execute external actions and does not call Twitch, OBS, Ollama, WinHTTP, or dashboard APIs.

It establishes normalized event types, policy-controlled action proposals, bounded in-memory duplicate suppression by EventSub ID, a bounded decision history, and counters for observed, proposed, duplicate, and rejected events.

## Safety boundary
The module only returns proposed actions. There is no execute API in this increment. AiManagerStatus.monitor_only is always true.

A proposal is not proof that an external operation succeeded. A future Action Dispatcher must re-check OAuth scopes, Twitch rate limits, current stream state, OBS connectivity, feature toggles, and permissions immediately before execution.

Chat text and event payloads are untrusted data. They must never change policy or grant permissions.

## Initial policies
- Incoming raid: propose native Shoutout only when enabled, raider identity is present, stream is online, and viewer count is at least one.
- Follow: propose welcome only when enabled and login is present.
- Stream start/end: propose session tracking when enabled.
- Component degraded: propose owner notification when enabled.
- Chat: propose AI reply only for an explicit mention.
- Ordinary chat and OBS changes: observe only.

These are proposals only. Existing application code remains responsible for side effects.

## Integration contract
This module is caller-thread-only. The current application may feed normalized observations from its existing main/EventSub loop. If worker threads are introduced, synchronization or a thread-safe queue must be added before cross-thread use.

Event IDs are kept in a bounded 64-entry ring. This suppresses immediate duplicate deliveries but is not durable across restarts. Restart-safe idempotency belongs in the future storage layer.

## Build and test
From the repository root:
cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_ai_manager.c src/ai_manager.c -o test_ai_manager
./test_ai_manager

## Runtime integration (added)
The main bot loop initializes the manager in monitor-only mode and synchronizes its policy with the saved raid Shoutout and stream-report toggles. The manager receives:

- validated incoming raid and follow EventSub notifications after the existing achievement handler has accepted the event;
- direct bot mentions after the ordinary chat parser has accepted the message (ordinary chat is intentionally omitted to keep the bounded journal useful);
- confirmed stream ONLINE/OFFLINE transitions (not the initial state at startup);
- actual OBS scene changes observed by the dashboard's periodic OBS WebSocket status check (initial scene is a baseline).

The manager does **not** control the existing Twitch, OBS or reporting handlers. Those handlers continue their existing behaviour; a proposal in the journal is not an action execution record. The manager's event ID duplicate filter only controls its own proposals and does not yet deduplicate existing production side effects.

The local dashboard receives an `ai_manager` object through `GET /api/status`, containing `monitor_only`, `observed`, `proposed`, `duplicates`, `rejected`, and at most eight recent `decisions`. No chat content, OAuth token or OBS password is included. Counters and history reset when the bot restarts.

The dashboard renders actor logins and reasons via text nodes rather than HTML injection. The manager policy is refreshed after saved dashboard switches without clearing the journal.

## Next integration steps
1. Verify Windows/MinGW full application build and dashboard JSON integration tests.
2. Add a dry-run Action Dispatcher with centralized permissions.
3. Implement durable EventSub idempotency for production actions.
4. Add explicit per-action execution permissions and cooldown handling.
5. Test reconnection, repeat deliveries and full-queue behaviours before any autonomous execution.
