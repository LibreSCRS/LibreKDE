# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Build-time guard: fail if the card:// worker does not export the `kdemain`
# symbol. KIO 6's out-of-process kioworker helper dlopens the worker lib and
# resolves kdemain; without it the worker is rejected ("Could not find kdemain")
# and card:/ hangs. See components/kio/CardWorker.cpp.
#
# Invoked as: cmake -DNM=<nm> -DWORKER=<path-to-card.so> -P CheckKdemainExport.cmake

execute_process(
    COMMAND "${NM}" -D --defined-only "${WORKER}"
    OUTPUT_VARIABLE _syms
    RESULT_VARIABLE _rc)

if(NOT _rc EQUAL 0)
    message(WARNING "card://: could not run nm on '${WORKER}'; skipping the kdemain-export guard")
    return()
endif()

if(NOT _syms MATCHES "kdemain")
    message(FATAL_ERROR
        "card:// worker '${WORKER}' does NOT export 'kdemain'. KIO 6's kioworker "
        "helper resolves kdemain in the worker lib; without it the worker is "
        "rejected ('Could not find kdemain') and card:/ hangs. Restore the "
        "extern-\"C\" Q_DECL_EXPORT kdemain entry point in components/kio/CardWorker.cpp.")
endif()
