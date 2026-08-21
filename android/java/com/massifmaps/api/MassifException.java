package com.massifmaps.api;

/**
 * Anything the facade refused.
 *
 * Unchecked on purpose: a wrong property path or an unknown method is a programming mistake, not a
 * condition an app recovers from, and forcing a try/catch around every setter would make the sugar
 * worse than the flat API it wraps.
 */
public class MassifException extends RuntimeException {

    private final int result;

    MassifException(String message) {
        this(message, 0, null);
    }

    MassifException(String message, Throwable cause) {
        this(message, 0, cause);
    }

    MassifException(String message, int result, Throwable cause) {
        super(message, cause);
        this.result = result;
    }

    /** The underlying result code, or 0. See the facade's Result enum. */
    public int result() {
        return result;
    }

    static void check(int result, String verb, String path) {
        if (result == 0) {
            return;
        }
        throw new MassifException(verb + " '" + path + "' failed: " + describe(result), result, null);
    }

    private static String describe(int result) {
        switch (result) {
        case 1:  return "stale handle";
        case 2:  return "unknown class";
        case 3:  return "unknown property";
        case 4:  return "read-only";
        case 5:  return "unsupported type";
        case 6:  return "duplicate id";
        case 7:  return "not traversable - a dot into a scalar";
        case 8:  return "an object property on the way was not set";
        case 9:  return "bad spec";
        case 10: return "unknown type";
        case 11: return "unknown method";
        case 12: return "failed";
        case 13: return "rejected by the SDK";
        default: return "result " + result;
        }
    }
}
