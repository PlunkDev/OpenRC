if(NOT DEFINED OPENRC_EXECUTABLE OR OPENRC_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "OPENRC_EXECUTABLE was not provided")
endif()
if(NOT EXISTS "${OPENRC_EXECUTABLE}")
    message(FATAL_ERROR "OpenRC executable does not exist: ${OPENRC_EXECUTABLE}")
endif()
if(NOT DEFINED OPENRC_PE_INSPECTOR OR OPENRC_PE_INSPECTOR STREQUAL "")
    message(FATAL_ERROR "OPENRC_PE_INSPECTOR was not provided")
endif()
if(NOT EXISTS "${OPENRC_PE_INSPECTOR}")
    message(FATAL_ERROR "PE inspector does not exist: ${OPENRC_PE_INSPECTOR}")
endif()
if(NOT DEFINED OPENRC_PE_INSPECTOR_MODE)
    message(FATAL_ERROR "OPENRC_PE_INSPECTOR_MODE was not provided")
endif()
if(NOT OPENRC_PE_INSPECTOR_MODE STREQUAL "llvm-readobj" AND
   NOT OPENRC_PE_INSPECTOR_MODE STREQUAL "objdump")
    message(FATAL_ERROR
        "Unknown PE inspector mode: ${OPENRC_PE_INSPECTOR_MODE}")
endif()
if(NOT DEFINED OPENRC_EXPECTED_PE_MACHINE)
    message(FATAL_ERROR "OPENRC_EXPECTED_PE_MACHINE was not provided")
endif()

if(OPENRC_PE_INSPECTOR_MODE STREQUAL "llvm-readobj")
    execute_process(
        COMMAND "${OPENRC_PE_INSPECTOR}"
            --file-headers
            --coff-imports
            "${OPENRC_EXECUTABLE}"
        RESULT_VARIABLE openrc_inspector_result
        OUTPUT_VARIABLE openrc_metadata
        ERROR_VARIABLE openrc_inspector_error
    )
else()
    execute_process(
        COMMAND "${OPENRC_PE_INSPECTOR}" -f -p "${OPENRC_EXECUTABLE}"
        RESULT_VARIABLE openrc_inspector_result
        OUTPUT_VARIABLE openrc_metadata
        ERROR_VARIABLE openrc_inspector_error
    )
endif()

if(NOT openrc_inspector_result EQUAL 0)
    message(FATAL_ERROR
        "PE inspection failed for ${OPENRC_EXECUTABLE}: "
        "${openrc_inspector_error}")
endif()

string(TOLOWER "${openrc_metadata}" openrc_metadata_lower)
string(REGEX REPLACE "[ \t\r\n]+" "" openrc_metadata_compact
    "${openrc_metadata_lower}")

if(OPENRC_PE_INSPECTOR_MODE STREQUAL "llvm-readobj")
    if(OPENRC_EXPECTED_PE_MACHINE STREQUAL "amd64")
        set(openrc_expected_formats "format:coff-x86-64")
        set(openrc_expected_machines "machine:image_file_machine_amd64")
    elseif(OPENRC_EXPECTED_PE_MACHINE STREQUAL "i386")
        set(openrc_expected_formats "format:coff-i386")
        set(openrc_expected_machines "machine:image_file_machine_i386")
    else()
        message(FATAL_ERROR
            "Unsupported expected PE machine: ${OPENRC_EXPECTED_PE_MACHINE}")
    endif()
else()
    if(OPENRC_EXPECTED_PE_MACHINE STREQUAL "amd64")
        set(openrc_expected_formats
            "fileformatcoff-x86-64"
            "fileformatpei-x86-64"
        )
        set(openrc_expected_machines
            "architecture:x86_64"
            "architecture:i386:x86-64"
        )
    elseif(OPENRC_EXPECTED_PE_MACHINE STREQUAL "i386")
        set(openrc_expected_formats
            "fileformatcoff-i386"
            "fileformatpei-i386"
        )
        set(openrc_expected_machines "architecture:i386")
    else()
        message(FATAL_ERROR
            "Unsupported expected PE machine: ${OPENRC_EXPECTED_PE_MACHINE}")
    endif()
endif()

set(openrc_format_matches FALSE)
foreach(openrc_expected_format IN LISTS openrc_expected_formats)
    string(FIND "${openrc_metadata_compact}"
        "${openrc_expected_format}" openrc_format_position)
    if(NOT openrc_format_position EQUAL -1)
        set(openrc_format_matches TRUE)
    endif()
endforeach()
set(openrc_machine_matches FALSE)
foreach(openrc_expected_machine IN LISTS openrc_expected_machines)
    string(FIND "${openrc_metadata_compact}"
        "${openrc_expected_machine}" openrc_machine_position)
    if(NOT openrc_machine_position EQUAL -1)
        set(openrc_machine_matches TRUE)
    endif()
endforeach()
if(NOT openrc_format_matches OR NOT openrc_machine_matches)
    message(FATAL_ERROR
        "OpenRC executable has the wrong PE architecture: ${OPENRC_EXECUTABLE}")
endif()

string(REGEX MATCH
    "(name:|dllname:)(libc[+][+]|libunwind|libstdc[+][+]|libgcc|libwinpthread)[a-z0-9_.+-]*[.]dll"
    openrc_forbidden_runtime_import
    "${openrc_metadata_compact}"
)
if(NOT openrc_forbidden_runtime_import STREQUAL "")
    message(FATAL_ERROR
        "OpenRC executable imports forbidden compiler runtime family "
        "${openrc_forbidden_runtime_import}: ${OPENRC_EXECUTABLE}")
endif()
