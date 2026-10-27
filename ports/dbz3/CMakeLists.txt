cmake_minimum_required(VERSION 3.20)
project(dbz-budokai-3-switch
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Dragon Ball Z Budokai 3 Switch port"
)

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer Ninja if available
if(NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{CMAKE_GENERATOR})
    message(STATUS "Tip: use -G Ninja for faster builds")
endif()

# ---------------------------------------------------------------------------
# The two directories this project is built from
# ---------------------------------------------------------------------------
# PS3RECOMP_DIR is the toolkit checkout: include/, runtime/, libs/, tools/.
# RECOMP_DIR is where the lifter's output for THIS game lives. They are
# separate on purpose -- a port keeps its generated code in its own tree and
# points at whatever toolkit checkout it is building against:
#
#   cmake -B build -G Ninja \
#         -DPS3RECOMP_DIR=/path/to/ps3recomp \
#         -DRECOMP_DIR=/path/to/my_game/recompiled
#
if(NOT DEFINED PS3RECOMP_DIR)
    # This port lives at <workspace>/ports/dbz3.
    get_filename_component(PS3RECOMP_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/ps3recomp" ABSOLUTE)
endif()
get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)

set(RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../generated/dbz3/recompiled" CACHE PATH
    "Directory holding the lifter's output: ppu_recomp.h and the lifted sources")
get_filename_component(RECOMP_DIR "${RECOMP_DIR}" ABSOLUTE)

message(STATUS "ps3recomp SDK:   ${PS3RECOMP_DIR}")
message(STATUS "Lifted output:   ${RECOMP_DIR}")

if(NOT EXISTS "${RECOMP_DIR}/ppu_recomp.h")
    message(FATAL_ERROR
        "No ppu_recomp.h in ${RECOMP_DIR}.\n"
        "That header and the lifted sources beside it are what "
        "tools/ppu_lifter.py writes. Run the lifter, drop its output there, "
        "or point -DRECOMP_DIR= at wherever it already is.")
endif()

# The lifted translation units. A game's lift is hundreds of generated files,
# so they are globbed rather than listed; re-run CMake after a fresh lift.
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS
    "${RECOMP_DIR}/*.c"
    "${RECOMP_DIR}/*.cpp"
)
if(NOT RECOMP_SOURCES)
    message(FATAL_ERROR
        "${RECOMP_DIR} has a ppu_recomp.h but no .c or .cpp beside it. The "
        "lifted functions and the function table live in those; without them "
        "there is nothing to run.")
endif()
list(LENGTH RECOMP_SOURCES RECOMP_SOURCE_COUNT)
# Build-time rewrites of the lifted PPU sources (copies; the lift is untouched):
#   T39 PORT_SWITCH_PPU_VECNEON  AltiVec blocks as NEON (scripts/ppu_vecneon.py)
#   T38 PORT_SWITCH_PPU_FUNC_PROFILE  diagnostic: every function stamps itself
#       as the main thread's current function (scripts/ppu_instrument.py)
option(PORT_SWITCH_PPU_FUNC_PROFILE "Instrument lifted PPU functions for the Switch sampler" OFF)
if(PORT_SWITCH_PPU_FUNC_PROFILE)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        PORT_SWITCH_PPU_FUNC_PROFILE=1)
endif()
option(PORT_SWITCH_PPU_VECNEON "Rewrite lifted AltiVec blocks as NEON (Switch)" ON)
#   T40 PORT_SWITCH_PPU_LIFTFAST  fewer trampoline drains + cheaper guest memory
#       accessors (scripts/ppu_liftfast.py, dbz3_liftfast.h)
option(PORT_SWITCH_PPU_LIFTFAST "Lifted PPU: drop dead trampoline drains, lean memory accessors (Switch)" ON)
set(PPU_TU000 "${RECOMP_DIR}/ppu_recomp_000.cpp")
set(PPU_TU004 "${RECOMP_DIR}/ppu_recomp_004.cpp")
set(PPU_TU003 "${RECOMP_DIR}/ppu_recomp_003.cpp")
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" AND
   (PORT_SWITCH_PPU_FUNC_PROFILE OR PORT_SWITCH_PPU_VECNEON OR PORT_SWITCH_PPU_LIFTFAST))
    set(_ppu_py "${CMAKE_CURRENT_SOURCE_DIR}/../../.venv/Scripts/python.exe")
    set(_ppu_scripts "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts")
    set(_ppu_out "${CMAKE_CURRENT_BINARY_DIR}/ppu-rewrite")
    set(_ppu_new_sources "")
    set(_ppu_originals "")
    foreach(src IN LISTS RECOMP_SOURCES)
        get_filename_component(src_name "${src}" NAME)
        if(NOT src_name STREQUAL "ppu_hle_nids.cpp")
            list(APPEND _ppu_originals "${src}")
        endif()
    endforeach()
    foreach(src IN LISTS RECOMP_SOURCES)
        get_filename_component(src_name "${src}" NAME)
        get_filename_component(src_base "${src}" NAME_WE)
        if(src_name STREQUAL "ppu_hle_nids.cpp")   # no lifted functions; also HLE_NID_TABLE
            list(APPEND _ppu_new_sources "${src}")
            continue()
        endif()
        set(cur "${src}")
        if(PORT_SWITCH_PPU_FUNC_PROFILE)
            set(out "${_ppu_out}/${src_base}.prof.cpp")
            add_custom_command(OUTPUT "${out}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_ppu_out}"
                COMMAND "${_ppu_py}" -I "${_ppu_scripts}/ppu_instrument.py" "${cur}" "${out}"
                DEPENDS "${cur}" "${_ppu_scripts}/ppu_instrument.py" VERBATIM)
            set(cur "${out}")
        endif()
        if(PORT_SWITCH_PPU_VECNEON)
            set(out "${_ppu_out}/${src_base}.neon.cpp")
            add_custom_command(OUTPUT "${out}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_ppu_out}"
                COMMAND "${_ppu_py}" -I "${_ppu_scripts}/ppu_vecneon.py" "${cur}" "${out}"
                DEPENDS "${cur}" "${_ppu_scripts}/ppu_vecneon.py" VERBATIM)
            set(cur "${out}")
        endif()
        if(PORT_SWITCH_PPU_LIFTFAST)
            set(out "${_ppu_out}/${src_base}.fast.cpp")
            add_custom_command(OUTPUT "${out}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_ppu_out}"
                COMMAND "${_ppu_py}" -I "${_ppu_scripts}/ppu_liftfast.py" "${cur}" "${out}" ${_ppu_originals}
                DEPENDS "${cur}" ${_ppu_originals} "${_ppu_scripts}/ppu_liftfast.py"
                        "${CMAKE_CURRENT_SOURCE_DIR}/dbz3_liftfast.h" VERBATIM)
            set(cur "${out}")
        endif()
        list(APPEND _ppu_new_sources "${cur}")
        if(src_name STREQUAL "ppu_recomp_000.cpp")
            set(PPU_TU000 "${cur}")
        endif()
        if(src_name STREQUAL "ppu_recomp_004.cpp")
            set(PPU_TU004 "${cur}")
        endif()
        if(src_name STREQUAL "ppu_recomp_003.cpp")
            set(PPU_TU003 "${cur}")
        endif()
    endforeach()
    set(RECOMP_SOURCES ${_ppu_new_sources})
    message(STATUS "Lifted PPU:      rewritten copies (neon=${PORT_SWITCH_PPU_VECNEON} profile=${PORT_SWITCH_PPU_FUNC_PROFILE} liftfast=${PORT_SWITCH_PPU_LIFTFAST})")
endif()
message(STATUS "Lifted sources:  ${RECOMP_SOURCE_COUNT} file(s)")

# ---------------------------------------------------------------------------
# ps3recomp runtime library
# ---------------------------------------------------------------------------
# Built from source as a subdirectory rather than imported as a prebuilt
# archive. A static library carries none of its own link requirements, so an
# imported one arrives without SDL2, pthreads, the Metal frameworks or the
# Winsock/XInput imports -- the link then fails on every platform for a
# different reason. add_subdirectory hands all of that over with the target.
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)
option(PORT_DBZ3_DEKO_DIAG "Enable detailed deko3d texture and draw diagnostics" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" AND PORT_DBZ3_DEKO_DIAG)
    set_property(SOURCE "${PS3RECOMP_DIR}/libs/video/rsx_deko3d_backend.c"
        TARGET_DIRECTORY ps3recomp_runtime APPEND PROPERTY
        COMPILE_DEFINITIONS DBZ3_DEKO_DIAG=1)
endif()
option(PORT_SWITCH_INPUT_QUEUE_PROFILE "Measure Switch input queue age and depth" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" AND PORT_SWITCH_INPUT_QUEUE_PROFILE)
    set_property(SOURCE "${PS3RECOMP_DIR}/runtime/platform/switch_input.c"
        TARGET_DIRECTORY ps3recomp_runtime APPEND PROPERTY
        COMPILE_DEFINITIONS PORT_SWITCH_INPUT_QUEUE_PROFILE=1)
endif()

# ---------------------------------------------------------------------------
# The HLE NID table
# ---------------------------------------------------------------------------
# Maps each firmware import NID onto the handler in the runtime that answers
# it. A port may keep a hand-curated one beside its lift; otherwise generate
# the canonical every-module table, which is what the toolkit's own boot
# harness uses.
if(EXISTS "${RECOMP_DIR}/ppu_hle_nids.cpp")
    set(HLE_NID_TABLE "${RECOMP_DIR}/ppu_hle_nids.cpp")
    message(STATUS "HLE NID table:   ${HLE_NID_TABLE}")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                --out "${HLE_NID_TABLE}" --all
        DEPENDS "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                "${PS3RECOMP_DIR}/tools/nid_database.py"
        COMMENT "Generating the HLE NID -> handler table")
    message(STATUS "HLE NID table:   generated (every module)")
endif()

# ---------------------------------------------------------------------------
# The game executable
# ---------------------------------------------------------------------------
# The PPU boot scaffold is compiled here rather than taken from the runtime
# library: every one of these files #includes the lifter's per-game
# ppu_recomp.h, so they can only be built against a particular lift.
set(PS3RECOMP_SCAFFOLD
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
)

# ---------------------------------------------------------------------------
# Lifted SPU images (optional)
# ---------------------------------------------------------------------------
# A title that runs SPU work gets its images lifted into <SPU_DIR>/<image>/ by
# tools/spu_lifter.py, with a registration TU generated by
# tools/build_spu_workloads.py. cellSpurs dispatches them by content
# fingerprint, so the generated table is keyed to the images THIS build ships:
# a table generated from another release registers signatures that can never
# match, which is worse than shipping none. Both the table and the job-module
# registry self-register from constructors and are therefore optional -- a tree
# that has not lifted its SPU images yet configures and links without them.
set(SPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../generated/dbz3/spu" CACHE PATH
    "Directory holding the lifted SPU images (<SPU_DIR>/<image>/spu_recomp.c)")
option(PORT_ENABLE_SPU "Compile and register the title's lifted SPU images" ON)
option(PORT_SWITCH_SPU_PROFILE "Sample lifted SPU functions on Switch (diagnostic build)" OFF)

set(SPU_SOURCES "")
if(PORT_ENABLE_SPU)
    file(GLOB SPU_LIFTED CONFIGURE_DEPENDS "${SPU_DIR}/*/spu_recomp.c")
    if(SPU_LIFTED)
        list(LENGTH SPU_LIFTED SPU_IMAGE_COUNT)
        message(STATUS "Lifted SPU:      ${SPU_IMAGE_COUNT} image(s)")
        # The musttail indirect-branch fast path. Excluded from the runtime
        # library on purpose (see the SDK root CMakeLists): it must be built
        # here, by clang, or the tail call degrades to a plain call and a guest
        # loop that branches indirectly walks the thread stack off its guard
        # page a few thousand iterations in.
        # T30: on Switch, compile a build copy of each lift in which every
        # function keeps its SPU registers in locals (scripts/spu_regcache.py).
        option(PORT_SWITCH_SPU_REGCACHE "Keep SPU registers in locals in lifted SPU code (Switch)" ON)
        set(PORT_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/../../.venv/Scripts/python.exe" CACHE FILEPATH
            "Python used for build-time source rewrites")
        set(_spu_regcache_script "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/spu_regcache.py")
        option(PORT_SWITCH_SPU_FUSE "Fuse lifted SPU loop functions (Switch)" ON)
        set(PORT_SWITCH_SPU_FUSE_IMAGES "spu_0001_at_002E2580" CACHE STRING
            "SPU images whose loop functions are fused (DBZ3 bnusCore audio)")
        set(_spu_fuse_script "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/spu_fuse.py")
        set(_spu_profile_on OFF)
        if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" AND PORT_SWITCH_SPU_PROFILE)
            set(_spu_profile_on ON)
            set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUDOKAI_SPU_PROFILE=1)
        endif()
        foreach(lift IN LISTS SPU_LIFTED)
            get_filename_component(image_dir "${lift}" DIRECTORY)
            get_filename_component(image_name "${image_dir}" NAME)
            set(spu_src "${lift}")
            # T-A1: DBZ3 bnusCore reverb bypass (scripts/spu_patch_dbz3_audio.py),
            # every platform; runtime flag g_bnus_reverb_skip in stubs.cpp.
            if(image_name STREQUAL "spu_0001_at_002E2580" AND RECOMP_DIR MATCHES "generated[/\\]dbz3")
                set(patch_copy "${CMAKE_CURRENT_BINARY_DIR}/spu-patch/${image_name}.c")
                set(_patch_script "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/spu_patch_dbz3_audio.py")
                add_custom_command(OUTPUT "${patch_copy}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/spu-patch"
                    COMMAND "${PORT_PYTHON}" -I "${_patch_script}" "${spu_src}" "${patch_copy}" "${image_dir}"
                    DEPENDS "${spu_src}" "${_patch_script}"
                    VERBATIM)
                set(spu_src "${patch_copy}")
            endif()
            if(_spu_profile_on)
                set(profile_copy "${CMAKE_CURRENT_BINARY_DIR}/spu-profile/${image_name}.c")
                add_custom_command(OUTPUT "${profile_copy}"
                    COMMAND ${CMAKE_COMMAND} "-DINPUT=${lift}" "-DOUTPUT=${profile_copy}"
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/instrument-switch-spu.cmake"
                    DEPENDS "${lift}" "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/instrument-switch-spu.cmake"
                    VERBATIM)
                set(spu_src "${profile_copy}")
            endif()
            # T6.A1: fuse lifted functions that form pure-computation loops
            # into one function each (scripts/spu_fuse.py), so the loop stays
            # in one C function: no dispatcher round trip per transfer and
            # spu_regcache keeps the registers in NEON locals across it.
            if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" AND PORT_SWITCH_SPU_FUSE
                    AND image_name IN_LIST PORT_SWITCH_SPU_FUSE_IMAGES)
                set(fuse_copy "${CMAKE_CURRENT_BINARY_DIR}/spu-fuse/${image_name}.c")
                add_custom_command(OUTPUT "${fuse_copy}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/spu-fuse"
                    COMMAND "${PORT_PYTHON}" -I "${_spu_fuse_script}" "${spu_src}" "${fuse_copy}"
                    DEPENDS "${spu_src}" "${_spu_fuse_script}"
                    VERBATIM)
                set(spu_src "${fuse_copy}")
            endif()
            if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" AND PORT_SWITCH_SPU_REGCACHE)
                set(regcache_copy "${CMAKE_CURRENT_BINARY_DIR}/spu-regcache/${image_name}.c")
                add_custom_command(OUTPUT "${regcache_copy}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/spu-regcache"
                    COMMAND "${PORT_PYTHON}" -I "${_spu_regcache_script}"
                        "${spu_src}" "${regcache_copy}" "${image_dir}"
                    DEPENDS "${spu_src}" "${_spu_regcache_script}"
                    VERBATIM)
                set(spu_src "${regcache_copy}")
            endif()
            list(APPEND SPU_SOURCES "${spu_src}")
        endforeach()
        # The current BLUS lift emits g_spu_trampoline_fn transfers and
        # SPU_DRAIN, so it does not call the legacy musttail dispatcher.
        # GCC on Switch cannot compile __attribute__((musttail)).
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
            list(APPEND SPU_SOURCES
                "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c")
        endif()
        # Generated beside the lift; each is present only once its generator
        # has been run, so each is included only if it is actually here.
        foreach(gen IN ITEMS spu_workloads.c jobmods_register.c taskset_policy_bytes.c)
            if(EXISTS "${SPU_DIR}/${gen}")
                list(APPEND SPU_SOURCES "${SPU_DIR}/${gen}")
            else()
                message(STATUS "  (no ${gen} -- not registered)")
            endif()
        endforeach()
    else()
        message(STATUS "Lifted SPU:      none found in ${SPU_DIR}")
    endif()
endif()

add_executable(${PROJECT_NAME}
    main.cpp
    stubs.cpp
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
)
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    target_sources(${PROJECT_NAME} PRIVATE switch_app.c)
    # T23: inline guest loads/stores in the lifted code (ppu_recomp.h).
    option(PORT_PPU_FAST_VM "Inline plain guest memory access in lifted PPU code" ON)
    if(PORT_PPU_FAST_VM)
        set_property(SOURCE ${RECOMP_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS PPU_FAST_VM=1)
    endif()
    # T24: native fast path for DBZ3's inline vertex-constant upload (guest
    # 0x001FCAC4), hooked with --wrap. Only for a lift that has that function;
    # the code also checks the guest instructions at run time.
    option(PORT_DBZ3_GCM_CONST_FASTPATH "Native fast path for DBZ3 func_001FCAC4" ON)
    file(STRINGS "${RECOMP_DIR}/ppu_recomp.h" _dbz3_fcac4
         REGEX "^void func_001FCAC4\\(ppu_context\\* ctx\\);$")
    if(PORT_DBZ3_GCM_CONST_FASTPATH AND PORT_PPU_FAST_VM AND _dbz3_fcac4
            AND RECOMP_DIR MATCHES "generated[/\\]dbz3")
        target_sources(${PROJECT_NAME} PRIVATE dbz3_gcm_const_fastpath.cpp)
        # T36: native replacements for hot lifted PPU functions. The lifted
        # definitions are made weak in their own translation unit so the native
        # ones win for every caller (dbz3_weak_overrides.h).
        option(PORT_DBZ3_PPU_NATIVE "Native replacements for hot DBZ3 PPU functions" ON)
        if(PORT_DBZ3_PPU_NATIVE)
            target_sources(${PROJECT_NAME} PRIVATE dbz3_ppu_native.cpp)
            # Same FP contraction rule as the lifted code it replaces.
            set_property(SOURCE dbz3_ppu_native.cpp APPEND PROPERTY
                COMPILE_OPTIONS "-ffp-contract=off")
            set(_dbz3_weak_overrides ON)   # applied after the lifted-source options below
            message(STATUS "DBZ3 native PPU functions: ON")
        endif()
        target_link_options(${PROJECT_NAME} PRIVATE "LINKER:--wrap=_Z13func_001FCAC4P11ppu_context")
        message(STATUS "DBZ3 gcm-const fast path: ON")
        # T6.1: time each step of main()'s frame loop (t26-loop-prof.log), to
        # find the update/draw split for "2 logic steps per drawn frame".
        option(PORT_DBZ3_LOOP_PROF "Profile DBZ3 main loop steps" ON)
        if(PORT_DBZ3_LOOP_PROF)
            option(PORT_DBZ3_LEVEL_FUNCTION_PROFILE "Time func_00030790 during the level task" OFF)
            if(PORT_DBZ3_LEVEL_FUNCTION_PROFILE)
                target_compile_definitions(${PROJECT_NAME} PRIVATE PORT_DBZ3_LEVEL_FUNCTION_PROFILE=1)
            endif()
            target_sources(${PROJECT_NAME} PRIVATE dbz3_loop_prof.cpp)
            # A4: CRI -> surround-mixer output trace (t27-adx.log).
            target_sources(${PROJECT_NAME} PRIVATE dbz3_audio_trace.cpp)
            if(PORT_DBZ3_PPU_NATIVE)
                # A4: CRI error messages (weak override of func_00216DB4, TU 003).
                target_sources(${PROJECT_NAME} PRIVATE dbz3_crierr.cpp)
                set_property(SOURCE dbz3_crierr.cpp APPEND PROPERTY COMPILE_OPTIONS "-w;-ffp-contract=off")
            endif()
            target_link_options(${PROJECT_NAME} PRIVATE
                "LINKER:--wrap=_Z13func_0021ED98P11ppu_context")
            # T6.2b: allocator trace (alloc/free made weak in TU 000 by
            # dbz3_weak_overrides.h, so needs PORT_DBZ3_PPU_NATIVE).
            if(PORT_DBZ3_PPU_NATIVE)
                target_sources(${PROJECT_NAME} PRIVATE dbz3_heap_trace.cpp)
                set_property(SOURCE dbz3_heap_trace.cpp APPEND PROPERTY
                    COMPILE_OPTIONS "-w;-ffp-contract=off")
                set_property(SOURCE dbz3_loop_prof.cpp APPEND PROPERTY
                    COMPILE_DEFINITIONS PORT_DBZ3_HEAP_TRACE=1)
                if(PORT_DBZ3_LOGIC60)
                    set_property(SOURCE dbz3_heap_trace.cpp APPEND PROPERTY
                        COMPILE_DEFINITIONS PORT_DBZ3_LOGIC60=1)
                endif()
            endif()
            foreach(_fn 0004883C 00048088 0002E958 0001176C 0001C000 00015B10
                        0001573C 0001F318 00024588 00015EA8 0001745C 00028FD8
                        001F9DC0 000287F4 00015AB8 00015C54 0002E9A4 000487FC)
                target_link_options(${PROJECT_NAME} PRIVATE
                    "LINKER:--wrap=_Z13func_${_fn}P11ppu_context")
            endforeach()
            message(STATUS "DBZ3 main loop profile: ON")
            # T6.2: extra logic-only task passes so the game runs at 60 logic
            # steps/s whatever the drawn frame rate (dbz3_loop_prof.cpp).
            option(PORT_DBZ3_LOGIC60 "DBZ3 logic at 60 steps/s" ON)
            if(PORT_DBZ3_LOGIC60)
                set_property(SOURCE dbz3_loop_prof.cpp APPEND PROPERTY
                    COMPILE_DEFINITIONS PORT_DBZ3_LOGIC60=1)
                message(STATUS "DBZ3 logic at 60 Hz: ON")
            endif()
        endif()
    endif()
    # T33 test: run the DBZ3 bnusCore audio SPU at the lowest host priority.
    option(PORT_SWITCH_AUDIO_SPU_LOWPRIO "Lowest host priority for the bnusCore audio SPU" OFF)
    if(PORT_SWITCH_AUDIO_SPU_LOWPRIO)
        set_property(SOURCE stubs.cpp APPEND PROPERTY COMPILE_DEFINITIONS PORT_AUDIO_SPU_LOWPRIO=1)
        message(STATUS "Audio SPU low priority: ON")
    endif()
    # Which of the collection's PPU programs this runner boots. The games are
    # separate executables (PS3_GAME/USRDIR/LAUNCH/DBZ1.BIN, DBZ3.BIN) built
    # from the same port with RECOMP_DIR/SPU_DIR pointing at their own lift.
    set(PORT_GAME_ELF "sdmc:/switch/dbz-budokai-3-switch/game/PS3_GAME/USRDIR/LAUNCH/DBZ3.elf"
        CACHE STRING "SD path of the PPU ELF the Switch runner boots")
    set_property(SOURCE main.cpp switch_app.c APPEND PROPERTY
        COMPILE_DEFINITIONS "PORT_GAME_ELF=\"${PORT_GAME_ELF}\"")
    # T22: let the frame clock try the deko3d backend. Scoped to main.cpp
    # alone so the lifted TUs' compile commands do not change.
    if(PS3RECOMP_RSX_DEKO3D)
        set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS PORT_RSX_DEKO3D=1)
    endif()
endif()

# The lifted SPU sources use bare #include "spu_context.h". Scope that include
# directory (and their flags) to those TUs alone: adding it to the whole target
# changes the compile command of every lifted PPU TU, and those are large enough
# that the needless rebuild is measured in tens of minutes.
#   SPU_USE_DISPATCH_MT -- route legacy SPU_IB_DISPATCH through musttail.
#   -O2                 -- the target-wide -O1 exists for the huge PPU TUs; the
#                          SPU sources are small and decode-hot, so they get
#                          full optimization. The last -O on the line wins.
if(SPU_SOURCES)
    set_source_files_properties(${SPU_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        set_source_files_properties(${SPU_SOURCES} PROPERTIES
            COMPILE_DEFINITIONS SPU_USE_DISPATCH_MT)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        # T26: the lifted SPU code calls tiny helpers (local-store loads and
        # stores, shufb, fsmbi...) in its innermost DSP loops. GCC stopped
        # inlining them once a translation unit had grown past its default
        # budgets, so the bnusCore audio SPU spent its core on call overhead.
        # Raise the budgets for these TUs only.
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS
            "-O2;--param=inline-unit-growth=1000;--param=large-function-growth=1000;--param=max-inline-insns-single=400;--param=max-inline-insns-auto=100")
        # T27: no local-store diagnostics (watch/probe/SMC) in the lifted SPU
        # code -- a global load, compare and cold call on every LS access.
        option(PORT_SWITCH_SPU_LS_DIAG "Keep SPU local-store diagnostics in lifted SPU code" OFF)
        if(NOT PORT_SWITCH_SPU_LS_DIAG)
            set_property(SOURCE ${SPU_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS SPU_LS_NODIAG=1)
        endif()
    elseif(NOT MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")
    endif()
endif()

# app.rc carries the icon, the version block and the manifest, and needs a
# resource compiler. Listing it as a source on a host that has none is not a
# link error, it is a "no rule to make" for a file nothing can consume.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${RECOMP_DIR}"                            # the generated ppu_recomp.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../../generated/dbz3/inc"
    "${PS3RECOMP_DIR}/include"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/memory"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/runtime/platform"        # win32_compat.h and friends
    "${PS3RECOMP_DIR}/libs/video"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ps3recomp_runtime
)

# The runtime is C++, and on Apple also Objective-C. Letting CMake infer the
# link language from the generated C would leave the C++ standard library out.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Windows system import libraries. dbghelp symbolizes crash-handler addresses
# against the PDB, user32 and gdi32 are what the backend's window and its GDI
# present are made of, and winmm is timeBeginPeriod. None of the four names
# anything a POSIX linker can find; the D3D12 backend's own imports come in
# through the #pragma comment(lib) in libs/video/rsx_d3d12_backend.c.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dbghelp
        user32
        gdi32
        winmm
        dxguid
    )
endif()

# ---------------------------------------------------------------------------
# Compiler-specific flags
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fno-strict-aliasing
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3 /wd4100 /wd4244 /wd4267
        /Zc:__cplusplus         # report correct __cplusplus value
        # ppu_recomp.h defines PPU_FENCE, the lowering of the guest's
        # sync/lwsync/eieio/isync, and in C that is <stdatomic.h>. The lifter
        # writes C++ by default, but --single-file writes ppu_recomp.c, and a
        # port that took it would not compile without this. The runtime's own
        # build already carries the same flag for the same reason.
        /experimental:c11atomics
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# The lifted translation units are machine-generated and can run to hundreds of
# thousands of lines each, so they get their own flags:
#
#   - warnings off. They are about generated code and nobody will act on them,
#     and at this volume they bury the diagnostics that matter.
#   - no floating-point contraction. arm64 fuses a multiply and an add by
#     default; a PPC fmadd is spelt as one where the lifter means one.
#   - /bigobj on MSVC. A single lifted file routinely passes the 65,536-section
#     limit of the default object format.
if(MSVC)
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off")
endif()
if(_dbz3_weak_overrides)
    # After the line above, which replaces the lifted sources' options.
    set_property(SOURCE "${PPU_TU000}" "${PPU_TU003}" "${PPU_TU004}" APPEND PROPERTY
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/dbz3_weak_overrides.h")
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
