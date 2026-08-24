/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_METHODS_H_
#define _MASSIF_API_METHODS_H_

#include "api/Context.h"
#include "core/Variant.h"

#include <string>
#include <vector>

namespace massif {
    class MapBounds;
    class MapPos;
    class MapTile;
    class Projection;

    namespace api {

    /**
     * A call's arguments, as a JSON array.
     *
     * An array rather than an object because a method's parameters are positional in every
     * language the facade binds to, and naming them would mean maintaining a second name per
     * parameter. Each getter reports whether the argument was there and of the right shape, so a
     * thunk validates as it reads instead of trusting the caller.
     */
    class CallArgs {
    public:
        CallArgs();

        /**
         * Parses a JSON array. An empty string is an empty argument list.
         * @return False when the JSON does not parse or is not an array.
         */
        static bool parse(const std::string& json, CallArgs& args);

        int count() const;

        bool getBool(int index, bool& value) const;
        bool getLong(int index, long long& value) const;
        bool getDouble(int index, double& value) const;
        bool getString(int index, std::string& value) const;
        /**
         * Another object, by its handle - the argument shape a method takes when it needs one
         * (findFeatures takes a SearchRequest). Resolve it with Context::getObject, which is what
         * checks the class; this only reads the number.
         */
        bool getHandle(int index, Handle& value) const;
        /**
         * A position, as [x, y] or [x, y, z], converted into the OBJECT's own projection.
         *
         * An argument arrives in the same projection a property read hands back - WGS84 unless the
         * caller named one - so moveTo takes the position screenToMap just returned. Both sides go
         * through this pair; a thunk never sees a projection.
         */
        bool getPos(int index, MapPos& value) const;
        /** An array of positions, converted the same way. */
        bool getPositions(int index, std::vector<MapPos>& value) const;
        /**
         * The same pair, but stopping at WGS84 - for an SDK method that takes WGS84 and reprojects
         * internally. Handing those the object's projection converts twice: the elevation query
         * then read metres as degrees and answered "no data" for every position.
         */
        bool getPosWgs84(int index, MapPos& value) const;
        bool getPositionsWgs84(int index, std::vector<MapPos>& value) const;
        /** Bounds, as a pair of positions, converted the same way. */
        bool getBounds(int index, MapBounds& value) const;
        /** The other direction: a position a thunk PRODUCES, ready to hand back. */
        MapPos toCaller(const MapPos& value) const;
        /** A tile, as [x, y, zoom]. */
        bool getTile(int index, MapTile& value) const;
        /** The raw argument, for a thunk that takes free-form JSON. */
        Variant get(int index) const;

        /**
         * The projections a position argument is converted BETWEEN: the caller's and the object's.
         * Set by Context::call; either being null leaves positions alone.
         */
        void setProjections(const std::shared_ptr<Projection>& caller,
                            const std::shared_ptr<Projection>& object);

    private:
        Variant _array;
        std::shared_ptr<Projection> _caller;
        std::shared_ptr<Projection> _object;
    };

    /**
     * One method's implementation.
     *
     * The context is passed so a thunk that returns an object can register it - a result the
     * caller then addresses by handle, which is how a binary blob crosses the boundary without
     * being copied into a string.
     * @return RESULT_OK, or a code the caller reports. RESULT_BAD_SPEC for a bad argument list.
     */
    typedef Result (*MethodInvoke)(Context& context, void* obj, const CallArgs& args,
                                   PropertyValue& result);

    /**
     * Methods an object can be asked to run, by name.
     *
     * A separate table from the property one, and hand-registered rather than generated: a
     * property is declared by a Swig macro the generator can read, a method is an ordinary C++
     * signature, and every method needs argument decoding written for it anyway. Registering is
     * still data, not another verb - see the design doc.
     */
    /**
     * What a .i file DECLARES: read by scripts/gen-api-tables.py, emitted as MethodDecls.inc, and
     * checked against the registry at startup. A method registered but not declared is invisible
     * to every autocompletion emitter; one declared but not registered completes to a call that
     * fails. Neither shows up without the check.
     */
    struct MethodDecl {
        const char* cppClass;
        const char* name;
        int argCount;
    };

    struct EventDecl {
        const char* cppClass;
        const char* name;
    };

    namespace Methods {

        /** Reports any method the .i files declare and this build did not register, or vice versa. */
        void checkDeclarations();


        /**
         * Adds a method to a class. Registering the same name twice replaces it, which is what
         * lets a plugin specialise one.
         */
        void registerMethod(const char* cppClass, const char* name, MethodInvoke invoke);

        /**
         * Looks a method up, walking the class' base chain, so a method declared on
         * TileDataSource is callable on any source.
         * @return The implementation, or null.
         */
        MethodInvoke findMethod(const char* cppClass, const std::string& name);

        /**
         * Registers the SDK's own methods. Called once, lazily, by the binding - the built-ins
         * live in their own translation unit so a test can link this one without every SDK class.
         */
        void registerBuiltins();

    }

} }

#endif
