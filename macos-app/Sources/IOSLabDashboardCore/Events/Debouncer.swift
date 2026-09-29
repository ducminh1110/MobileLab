import Foundation

/// Collapses a burst of triggers into one call: the action runs `delay` seconds after the last trigger.
/// The backend emits dozens of events per job; without this every one of them would refetch the lists.
public actor Debouncer {
    public typealias Sleep = @Sendable (TimeInterval) async throws -> Void

    private let delay: TimeInterval
    private let sleep: Sleep
    private let action: @Sendable () async -> Void
    private var pending: Task<Void, Never>?
    private var generation = 0

    public init(
        delay: TimeInterval,
        sleep: @escaping Sleep = { seconds in try await Task.sleep(nanoseconds: UInt64(max(0, seconds) * 1_000_000_000)) },
        action: @escaping @Sendable () async -> Void
    ) {
        self.delay = delay
        self.sleep = sleep
        self.action = action
    }

    public func trigger() {
        pending?.cancel()
        generation += 1
        let mine = generation
        let delay = self.delay, sleep = self.sleep, action = self.action
        pending = Task { [weak self] in
            do { try await sleep(delay) } catch { return }
            if Task.isCancelled { return }
            await self?.fire(generation: mine, action: action)
        }
    }

    private func fire(generation mine: Int, action: @Sendable () async -> Void) async {
        guard mine == generation else { return }
        await action()
    }

    public func cancel() {
        pending?.cancel()
        pending = nil
        generation += 1
    }
}
