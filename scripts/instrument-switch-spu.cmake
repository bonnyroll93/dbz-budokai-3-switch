# Build copies only: keep the checked-in lift unchanged.
file(READ "${INPUT}" source)
get_filename_component(source_dir "${INPUT}" DIRECTORY)
get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(RELATIVE_PATH header_path "${output_dir}" "${source_dir}/spu_recomp.h")
string(REPLACE "#include \"spu_recomp.h\""
    "#include \"${header_path}\"\n#include \"spu_profile.h\"" source "${source}")
set(pattern "void ([A-Za-z0-9_]+)_spu_func_([0-9A-Fa-f]+)\\(spu_context\\* ctx\\) \\{")
string(REGEX MATCHALL "${pattern}" entries "${source}")
list(LENGTH entries count)
if(count EQUAL 0)
    message(FATAL_ERROR "No lifted function entries in ${INPUT}")
endif()
string(REGEX REPLACE "${pattern}"
    "void \\1_spu_func_\\2(spu_context* ctx) {\n    SPU_PROFILE_SCOPE(ctx->image_id, 0x\\2u);"
    source "${source}")
file(WRITE "${OUTPUT}" "${source}")
message(STATUS "SPU profiler: ${count} entries from ${INPUT}")
