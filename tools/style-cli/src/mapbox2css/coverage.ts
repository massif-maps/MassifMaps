/**
 * What the translation could not carry. Every drop is counted and named: a converter that stays
 * quiet about what it skipped reads as "fully converted" when it was not.
 */
export class Coverage {
    readonly emitted = new Map<string, number>();
    readonly dropped = new Map<string, { count: number; reason: string; layers: Set<string> }>();
    readonly notes: string[] = [];

    emit(property: string): void {
        this.emitted.set(property, (this.emitted.get(property) ?? 0) + 1);
    }

    drop(property: string, reason: string, layerId: string): void {
        const entry = this.dropped.get(property) ?? { count: 0, reason, layers: new Set<string>() };
        entry.count++;
        entry.layers.add(layerId);
        this.dropped.set(property, entry);
    }

    note(message: string): void {
        this.notes.push(message);
    }

    get emittedCount(): number {
        return [...this.emitted.values()].reduce((a, b) => a + b, 0);
    }

    get droppedCount(): number {
        return [...this.dropped.values()].reduce((a, b) => a + b.count, 0);
    }

    report(): string {
        const total = this.emittedCount + this.droppedCount;
        const pct = total === 0 ? 100 : Math.round((this.emittedCount / total) * 100);
        const lines = [`Coverage: ${this.emittedCount}/${total} properties (${pct}%)`];

        if (this.dropped.size > 0) {
            lines.push('', 'Dropped:');
            const ranked = [...this.dropped.entries()].sort((a, b) => b[1].count - a[1].count);
            for (const [property, { count, reason, layers }] of ranked) {
                const where = layers.size <= 3 ? [...layers].join(', ') : `${layers.size} layers`;
                lines.push(`  ${String(count).padStart(4)}x  ${property.padEnd(34)} ${reason}  (${where})`);
            }
        }
        if (this.notes.length > 0) {
            lines.push('', 'Notes:', ...this.notes.map((n) => `  ${n}`));
        }
        return lines.join('\n');
    }
}
