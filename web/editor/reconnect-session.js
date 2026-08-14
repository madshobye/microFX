export class ReconnectSession {
  constructor(options = {}) {
    this.defaultTimeoutMs = options.timeoutMs ?? 60000;
    this.protocol = null;
    this.waiters = new Set();
    this.cancelled = null;
  }

  attach(protocol) {
    if (!protocol) throw new Error("Cannot attach an empty protocol");
    this.protocol = protocol;
    this.cancelled = null;
    for (const waiter of this.waiters) waiter.resolve(protocol);
    this.waiters.clear();
  }

  detach(protocol = this.protocol) {
    if (this.protocol === protocol) this.protocol = null;
  }

  cancel(reason = "Operation cancelled") {
    this.protocol = null;
    this.cancelled = new Error(reason);
    for (const waiter of this.waiters) waiter.reject(this.cancelled);
    this.waiters.clear();
  }

  wait(options = {}) {
    if (this.protocol) return Promise.resolve(this.protocol);
    if (this.cancelled) return Promise.reject(this.cancelled);
    const timeoutMs = options.timeoutMs ?? this.defaultTimeoutMs;
    return new Promise((resolve, reject) => {
      const waiter = { resolve: (protocol) => { clearTimeout(waiter.timer); resolve(protocol); },
        reject: (error) => { clearTimeout(waiter.timer); reject(error); }, timer: null };
      waiter.timer = setTimeout(() => {
        this.waiters.delete(waiter);
        reject(new Error("Timed out waiting for device reconnection"));
      }, timeoutMs);
      this.waiters.add(waiter);
    });
  }

  async retry(operation, options = {}) {
    const timeoutMs = options.timeoutMs ?? this.defaultTimeoutMs;
    const deadline = Date.now() + timeoutMs;
    let attempts = 0;
    while (Date.now() <= deadline) {
      const protocol = await this.wait({ timeoutMs: Math.max(1, deadline - Date.now()) });
      attempts += 1;
      try {
        return await operation(protocol, attempts);
      } catch (error) {
        // Only transport replacement is retryable. A command rejected by the
        // currently attached device is a real error and must be surfaced.
        if (this.protocol === protocol) throw error;
        options.onRetry?.({ attempts, error });
      }
    }
    throw new Error("Device reconnection timed out");
  }

  request(type, fields = {}, options = {}) {
    return this.retry((protocol) => protocol.request(type, fields), options);
  }
}
