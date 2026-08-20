/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_MASSIFAPI_H_
#define _MASSIF_API_MASSIFAPI_H_

#include "api/Context.h"

#include <memory>
#include <string>

namespace massif {
    class Options;

    namespace api {

    /**
     * The facade API, as an app sees it.
     *
     * Experimental and incomplete: only the property verbs exist so far, and they address the
     * default context. See https://github.com/massif-maps/MassifMaps/issues/146.
     *
     * Every call returns a result code rather than throwing, because this is a verification
     * surface rather than the final binding.
     */
    class MassifApi {
    public:
        /**
         * Adopts an object built with the object API, so it can be addressed by id and handle.
         * One overload per kind, and the set of kinds is closed.
         * @param kind The namespace, e.g. "options". Ids only collide within a kind.
         * @param objectId The caller's name for the object. "id" is a keyword in Objective-C.
         * @param options The object.
         * @return The handle, or 0 when the id is already taken.
         */
        static int registerOptions(const std::string& kind, const std::string& objectId,
                                   const std::shared_ptr<Options>& options);

        /**
         * Drops an id and the context's reference to the object behind it.
         * @return True when the id existed.
         */
        static bool unregisterObject(const std::string& kind, const std::string& objectId);

        /**
         * Returns the handle registered under a kind and id, or 0.
         */
        static int findObject(const std::string& kind, const std::string& objectId);

        /**
         * Writes a property. The path may walk object properties: "fogOptions.rangeStart".
         * @return 0 on success, see the Result enum otherwise.
         */
        static int setFloat(int handle, const std::string& path, double value);
        /**
         * @copydoc MassifApi::setFloat
         */
        static int setInt(int handle, const std::string& path, long long value);
        /**
         * @copydoc MassifApi::setFloat
         */
        static int setBool(int handle, const std::string& path, bool value);
        /**
         * @copydoc MassifApi::setFloat
         */
        static int setString(int handle, const std::string& path, const std::string& value);

        /**
         * Reads a property. Returns the fallback when the path does not resolve, so a caller
         * that does not care about the reason does not have to check twice.
         */
        static double getFloat(int handle, const std::string& path, double defaultValue);
        /**
         * @copydoc MassifApi::getFloat
         */
        static long long getInt(int handle, const std::string& path, long long defaultValue);
        /**
         * @copydoc MassifApi::getFloat
         */
        static bool getBool(int handle, const std::string& path, bool defaultValue);
        /**
         * @copydoc MassifApi::getFloat
         */
        static std::string getString(int handle, const std::string& path, const std::string& defaultValue);

    private:
        MassifApi();
    };

} }

#endif
