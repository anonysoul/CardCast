export class FrameStats {
    constructor() {
        this.received = this.missing = this.trimmed = this.resync = 0;
        this.buckets = Array.from({ length: 10 }, () => ({ second: -1, received: 0, missing: 0, trimmed: 0, resync: 0 }));
    }
    add(kind, amount, now) {
        this[kind] += amount;
        const second = Math.floor(now / 1000), index = second % 10;
        if (this.buckets[index].second !== second) {
            this.buckets[index] = { second, received: 0, missing: 0, trimmed: 0, resync: 0 };
        }
        this.buckets[index][kind] += amount;
    }
    recentDropPercent(now) {
        const second = Math.floor(now / 1000);
        let expected = 0, discarded = 0;
        for (const bucket of this.buckets) {
            if (bucket.second < second - 9 || bucket.second > second) continue;
            expected += bucket.received + bucket.missing;
            discarded += bucket.missing + bucket.trimmed + bucket.resync;
        }
        return expected ? Math.min(100, discarded * 100 / expected) : 0;
    }
}
