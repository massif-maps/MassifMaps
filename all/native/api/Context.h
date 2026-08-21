/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_CONTEXT_H_
#define _MASSIF_API_CONTEXT_H_

#include "api/PropertyTable.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace massif { namespace api {

    /**
     * An opaque reference to a registered object.
     *
     * 20 bits of slot index and 12 bits of generation, so a handle held past a destroy is
     * detected rather than silently addressing whatever took the slot. 1M live objects and 4096
     * reuses of a slot before the generation wraps; it fits a uint32_t, and therefore also a
     * JavaScript number, which is what the C and WASM bindings need.
     */
    typedef std::uint32_t Handle;

    static const Handle NULL_HANDLE = 0;

    enum Result {
        RESULT_OK = 0,
        RESULT_BAD_HANDLE,       // never registered, or freed and the generation moved on
        RESULT_UNKNOWN_CLASS,    // the object's class declares no properties
        RESULT_UNKNOWN_PROPERTY, // dropped with a warning by callers that tolerate it
        RESULT_READONLY,
        RESULT_UNSUPPORTED_TYPE, // STRUCT, and writing an OBJECT, until their accessors land
        RESULT_DUPLICATE_ID,
        RESULT_NOT_TRAVERSABLE,  // a dotted path crossed something that is not an OBJECT
        RESULT_NULL_OBJECT,      // an OBJECT property on the way was not set
        RESULT_BAD_SPEC,         // not a JSON object, or it does not parse
        RESULT_UNKNOWN_TYPE      // no factory builds that "type"
    };

    /**
     * How a spec key that does not resolve is treated. Unknown keys are dropped with a warning,
     * so a spec written against another SDK version still applies what it can.
     */
    enum SpecTolerance {
        SPEC_TOLERANT = 0,
        SPEC_STRICT
    };

    /**
     * Owns the handle table and the per-kind id registries.
     *
     * There is one default context, which is what the static bindings use, but nothing is a raw
     * global: a second context is a second isolated world, which is what tests and a WASM module
     * instance want.
     */
    class Context {
    public:
        Context();
        virtual ~Context();

        /**
         * The context the static bindings address. Created on first use.
         */
        static const std::shared_ptr<Context>& GetDefault();

        /**
         * Registers an object under a kind and id.
         * @param kind The namespace, e.g. "source". Ids only collide within a kind.
         * @param id The caller's name for the object.
         * @param obj The object. The context keeps it alive.
         * @param cppClass Its fully qualified C++ name, e.g. "massif::FogOptions".
         * @param handle Set to the new handle on success.
         * @return RESULT_OK, or RESULT_DUPLICATE_ID when the id is taken.
         */
        Result registerObject(const std::string& kind, const std::string& id,
                              const std::shared_ptr<void>& obj, const char* cppClass,
                              Handle& handle);

        /**
         * Builds an object from a JSON spec and registers it under a kind and id.
         *
         * Creating an id that already exists with an IDENTICAL spec returns the existing handle,
         * which is how two maps come to share one source without coordinating. A different spec
         * under the same id is an error, never a silent replace.
         *
         * Keys the factory does not consume are applied as properties, so an option needs no
         * work here. An unknown key is dropped with a warning.
         *
         * @param kind The object kind, e.g. "source".
         * @param id The caller's name for the object.
         * @param json The spec.
         * @param handle Set to the handle on success.
         */
        Result create(const std::string& kind, const std::string& id, const std::string& json,
                      Handle& handle);

        /**
         * Returns the handle registered under a kind and id, or NULL_HANDLE.
         */
        Handle findObject(const std::string& kind, const std::string& id) const;

        /**
         * Returns the object behind a handle, or null when the handle is stale.
         */
        std::shared_ptr<void> getObject(Handle handle) const;

        /**
         * Drops the id and, with it, the context's reference to the object. Handles held
         * elsewhere become stale rather than dangling.
         * @return True when the id existed.
         */
        bool unregisterObject(const std::string& kind, const std::string& id);

        /**
         * Reads a property of a registered object.
         */
        Result getProperty(Handle handle, const std::string& path, PropertyValue& value) const;

        /**
         * Writes a property of a registered object. The underlying setter is called, so the
         * change reaches the renderer exactly as a direct call would.
         */
        Result setProperty(Handle handle, const std::string& path, const PropertyValue& value);

        /**
         * The number of live handles. For tests and leak checks.
         */
        std::size_t getObjectCount() const;

    private:
        struct Slot {
            std::shared_ptr<void> obj;
            const char* cppClass = nullptr;
            std::uint32_t generation = 1;
            bool used = false;
            // The spec this was built from, canonicalised, so an identical create can be
            // recognised as a reuse rather than a conflict.
            std::string spec;
        };

        static const int INDEX_BITS = 20;
        static const std::uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;
        static const std::uint32_t MAX_GENERATION = (1u << (32 - INDEX_BITS)) - 1;

        Handle allocate(const std::shared_ptr<void>& obj, const char* cppClass);
        const Slot* resolve(Handle handle) const;
        Handle findObjectLocked(const std::string& kind, const std::string& id) const;
        // Walks a dotted path, leaving the object owning the final segment in target.
        const PropertyEntry* lookup(Handle handle, const std::string& path,
                                    ObjectRef& target, Result& result) const;

        mutable std::mutex _mutex;
        std::vector<Slot> _slots;
        std::vector<std::uint32_t> _freeSlots;
        std::unordered_map<std::string, std::unordered_map<std::string, Handle> > _ids;
    };

} }

#endif
