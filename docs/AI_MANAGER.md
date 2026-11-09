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

## Next integration steps
1. Feed validated raid, follow, and chat EventSub events.
2. Feed stream transitions and OBS health changes.
3. Publish decision history and counters to the loopback dashboard.
4. Add an Action Dispatcher in dry-run mode.
5. Add explicit per-action execution permissions only after scenario tests pass.
