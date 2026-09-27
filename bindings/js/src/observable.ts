/** The part of NativeScript's Observable the surface API uses, so the same classes run anywhere. */
export interface EventData {
    eventName: string;
    object: any;
}

interface Listener {
    callback: (data: EventData) => void;
    thisArg?: any;
    once: boolean;
}

export class Observable {
    private readonly mListeners: { [event: string]: Listener[] } = {};

    on(eventNames: string, callback: (data: EventData) => void, thisArg?: any) {
        this.addEventListener(eventNames, callback, thisArg);
    }

    once(eventNames: string, callback: (data: EventData) => void, thisArg?: any) {
        this.addEventListener(eventNames, callback, thisArg, true);
    }

    off(eventNames: string, callback?: (data: EventData) => void, thisArg?: any) {
        this.removeEventListener(eventNames, callback, thisArg);
    }

    /** `eventNames` may list several, comma-separated, as NativeScript's does. */
    addEventListener(eventNames: string, callback: (data: EventData) => void, thisArg?: any, once = false) {
        for (const name of names(eventNames)) {
            (this.mListeners[name] ??= []).push({ callback, thisArg, once });
        }
    }

    removeEventListener(eventNames: string, callback?: (data: EventData) => void, thisArg?: any) {
        for (const name of names(eventNames)) {
            const kept = (this.mListeners[name] ?? []).filter((l) => callback && (l.callback !== callback || (thisArg !== undefined && l.thisArg !== thisArg)));
            if (kept.length) {
                this.mListeners[name] = kept;
            } else {
                delete this.mListeners[name];
            }
        }
    }

    hasListeners(eventName: string): boolean {
        return !!this.mListeners[eventName]?.length;
    }

    notify<T extends EventData>(data: T) {
        const listeners = this.mListeners[data.eventName];
        if (!listeners) {
            return;
        }
        for (const listener of listeners.slice()) {
            if (listener.once) {
                this.removeEventListener(data.eventName, listener.callback, listener.thisArg);
            }
            listener.callback.call(listener.thisArg, data);
        }
    }
}

function names(eventNames: string): string[] {
    return eventNames.split(',').map((name) => name.trim()).filter(Boolean);
}
