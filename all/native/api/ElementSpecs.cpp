/*
 * The vector-element kinds, which are the one place a spec cannot come straight from a constructor.
 *
 * A style like MarkerStyle takes 17 positional arguments, all required, including a Bitmap - which
 * is why the SDK has a builder for it. So the JSON schema for a style is its BUILDER's attributes,
 * which the property table already carries: build the builder, let the property pass fill it, then
 * finalise. Nothing here grows when a style gains a property.
 */

#include "api/ElementSpecs.h"
#include "api/Spec.h"
#include "api/SpecBuilders.h"
#include "styles/BalloonPopupStyleBuilder.h"
#include "styles/MarkerStyleBuilder.h"
#include "utils/Log.h"

#include <map>
#include <memory>
#include <string>

namespace massif { namespace api {

    namespace {

        /** What buildStyle() returns, and how to call it: it is not virtual on StyleBuilder. */
        struct StyleFinaliser {
            const char* styleClass;
            std::shared_ptr<void> (*build)(void* builder);
        };

        std::map<std::string, StyleFinaliser>& finalisers() {
            static std::map<std::string, StyleFinaliser> instance;
            return instance;
        }

        template <typename Builder, typename Style>
        void registerFinaliser(const char* builderClass, const char* styleClass) {
            StyleFinaliser finaliser;
            finaliser.styleClass = styleClass;
            finaliser.build = [](void* builder) {
                return std::static_pointer_cast<void>(
                    static_cast<Builder*>(builder)->buildStyle());
            };
            finalisers()[builderClass] = finaliser;
        }

        Result buildElementStyle(Context& context, const Variant& spec, ObjectRef& object,
                                 std::set<std::string>& consumed) {
            ObjectRef builder;
            Result result = buildFromConstructor(context, "elementstyle", spec, builder, consumed);
            if (result != RESULT_OK) {
                return result;
            }
            applySpecProperties(builder, spec, consumed);

            auto it = finalisers().find(builder.cppClass);
            if (it == finalisers().end()) {
                Log::Errorf("Spec: no finaliser for %s", builder.cppClass);
                return RESULT_UNKNOWN_TYPE;
            }
            object.obj = it->second.build(builder.obj.get());
            object.cppClass = it->second.styleClass;
            return object.obj ? RESULT_OK : RESULT_FAILED;
        }

        Result buildElement(Context& context, const Variant& spec, ObjectRef& object,
                            std::set<std::string>& consumed) {
            return buildFromConstructor(context, "element", spec, object, consumed);
        }

    }

    void registerElementFactories() {
        registerFinaliser<MarkerStyleBuilder, MarkerStyle>(
            "massif::MarkerStyleBuilder", "massif::MarkerStyle");
        registerFinaliser<BalloonPopupStyleBuilder, BalloonPopupStyle>(
            "massif::BalloonPopupStyleBuilder", "massif::BalloonPopupStyle");
        Spec::registerFactory("elementstyle", &buildElementStyle);
        Spec::registerFactory("element", &buildElement);
    }

} }
