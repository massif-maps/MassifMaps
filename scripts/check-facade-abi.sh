#!/bin/sh
# Fails when an SDK type reaches the facade's flat API.
#
# MassifApi is meant to be carriable by a hand-written binding - JNI, @objc, N-API, dart:ffi - and
# by the C ABI, which is only true while every signature is handles, strings, numbers and the
# facade's own EventListener/UiDispatcher. Anything naming an SDK class belongs in MassifInterop.
# See https://github.com/massif-maps/MassifMaps/issues/159.
set -e
cd "$(dirname "$0")/.."

status=0

# Pointer parameters and returns. Only the facade's own two callback interfaces are allowed.
bad=$(grep -o 'std::shared_ptr<[A-Za-z]*>' all/native/api/MassifApi.h |
      grep -v 'std::shared_ptr<EventListener>' |
      grep -v 'std::shared_ptr<UiDispatcher>' || true)
if [ -n "$bad" ]; then
    echo "check-facade-abi: SDK types in MassifApi.h signatures:" >&2
    echo "$bad" | sort -u >&2
    status=1
fi

# A forward declaration in namespace massif is how one gets there in the first place.
if grep -qE '^ *class [A-Za-z]+;' all/native/api/MassifApi.h; then
    echo "check-facade-abi: MassifApi.h forward-declares an SDK class - move the method to MassifInterop" >&2
    status=1
fi

# The Swig module must not pull an object-API proxy in behind it.
bad=$(grep '^%import' all/modules/api/MassifApi.i | grep -v '"api/' || true)
if [ -n "$bad" ]; then
    echo "check-facade-abi: MassifApi.i imports an object-API module:" >&2
    echo "$bad" >&2
    status=1
fi

[ $status -eq 0 ] && echo "check-facade-abi: MassifApi names no SDK type"
exit $status
