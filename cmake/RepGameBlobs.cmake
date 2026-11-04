# Blob embedding and shader preprocessing. Replaces the old
# `ld -r -b binary` + `objcopy --redefine-sym` rules: each blob is a generated
# .S that .incbin's the file, producing the same _binary_<prefix>_<name>_<ext>_{start,end}
# symbols MK_BLOB in include/binary_blobs.hpp expects. The .S is written with
# file(CONFIGURE) at configure time so it's only rewritten on change. The
# _WIN32 branch covers the mingw build; otherwise the .note.GNU-stack section
# keeps the linked stack non-executable (the old `-z noexecstack` blob flag).

# repgame_blob(<out_var> <prefix> <name> <ext> <input file>)
# Emits ${CMAKE_BINARY_DIR}/blobs/<prefix>_<name>_<ext>.S and returns it.
function(repgame_blob out_var prefix name ext input)
	set(asm ${CMAKE_BINARY_DIR}/blobs/${prefix}_${name}_${ext}.S)
	file(CONFIGURE OUTPUT ${asm} CONTENT "\
#if defined(_WIN32)
.section .rdata,\"dr\"
#else
.section .rodata
#endif
.global _binary_${prefix}_${name}_${ext}_start
_binary_${prefix}_${name}_${ext}_start:
.incbin \"${input}\"
.global _binary_${prefix}_${name}_${ext}_end
_binary_${prefix}_${name}_${ext}_end:
.global _binary_${prefix}_${name}_${ext}_size
.set _binary_${prefix}_${name}_${ext}_size, _binary_${prefix}_${name}_${ext}_end - _binary_${prefix}_${name}_${ext}_start
#if !defined(_WIN32)
.section .note.GNU-stack,\"\",@progbits
#endif
")
	# The assembler depfile doesn't track .incbin — depend on the embedded
	# file so content changes rebuild the blob.
	set_source_files_properties(${asm} PROPERTIES OBJECT_DEPENDS ${input})
	set(${out_var} ${asm} PARENT_SCOPE)
endfunction()

# repgame_preprocess_glsl(<out_dir> <out_list> DEFINES <d>...)
# Preprocess every src/shaders/*.glsl through the C preprocessor (same flags
# as make's shader_pp) into <out_dir>/<name>.glsl. Returns the outputs.
function(repgame_preprocess_glsl out_dir out_list)
	cmake_parse_arguments(PP "" "" "DEFINES" ${ARGN})
	set(processed)
	file(GLOB shader_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/shaders/*.glsl)
	foreach(src ${shader_srcs})
		get_filename_component(name ${src} NAME_WE)
		set(pp ${out_dir}/${name}.glsl)
		# The .d depfile must not land inside out_dir on wasm — that dir is the
		# preload fs and would ship the depfiles in RepGame.data.
		set(dep ${CMAKE_BINARY_DIR}/shader-deps/${name}.glsl.d)
		add_custom_command(OUTPUT ${pp}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shader-deps
			COMMAND gcc -E -P -x assembler-with-cpp -nostdinc -I ${CMAKE_SOURCE_DIR}/include ${PP_DEFINES}
				-MMD -MP -MT ${pp} -MF ${dep} ${src} -o ${pp}
			DEPFILE ${dep}
			DEPENDS ${src}
			COMMENT "shader_pp ${name}.glsl"
			VERBATIM
			COMMAND_EXPAND_LISTS)
		list(APPEND processed ${pp})
	endforeach()
	set(${out_list} ${processed} PARENT_SCOPE)
endfunction()

# repgame_preprocess_wgsl(<out_dir> <gen_dir> <out_list>)
# WGSL compute sources are dialect templates, not literal WGSL: same
# preprocessor trick as .glsl, plus -DREPGAME_WGSL_GEN which flips the
# dialect macros in wgsl_dialect.hpp, and -I <gen_dir> for the awk-generated
# block_ids.inc prelude (keeps block_definitions.hpp's enum the single
# source of truth).
function(repgame_preprocess_wgsl out_dir gen_dir out_list)
	set(block_ids ${gen_dir}/block_ids.inc)
	add_custom_command(OUTPUT ${block_ids}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${gen_dir}
		COMMAND awk -f ${CMAKE_SOURCE_DIR}/tools/gen_wgsl_block_ids.awk
			${CMAKE_SOURCE_DIR}/include/common/block_ids_gen.hpp > ${block_ids}
		DEPENDS ${CMAKE_SOURCE_DIR}/include/common/block_ids_gen.hpp
			${CMAKE_SOURCE_DIR}/tools/gen_wgsl_block_ids.awk
		COMMENT "gen_wgsl_block_ids.awk block_ids.inc"
		VERBATIM)
	set(processed)
	file(GLOB wgsl_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/shaders/*.wgsl)
	foreach(src ${wgsl_srcs})
		get_filename_component(name ${src} NAME_WE)
		set(pp ${out_dir}/${name}.wgsl)
		set(dep ${CMAKE_BINARY_DIR}/shader-deps/${name}.wgsl.d)
		add_custom_command(OUTPUT ${pp}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shader-deps
			COMMAND gcc -E -P -x assembler-with-cpp -nostdinc -I ${CMAKE_SOURCE_DIR}/include -I ${gen_dir} -DREPGAME_WGSL_GEN
				-MMD -MP -MT ${pp} -MF ${dep} ${src} -o ${pp}
			DEPFILE ${dep}
			DEPENDS ${src} ${block_ids}
			COMMENT "shader_pp ${name}.wgsl"
			VERBATIM)
		list(APPEND processed ${pp})
	endforeach()
	set(${out_list} ${processed} PARENT_SCOPE)
endfunction()

# repgame_shader_blobs(<out_blobs> <out_processed> DEFINES <d>...)
# Preprocess into ${CMAKE_BINARY_DIR}/shaders and emit a blob .S per
# preprocessed output.
function(repgame_shader_blobs out_blobs out_processed)
	cmake_parse_arguments(PP "" "" "DEFINES" ${ARGN})
	repgame_preprocess_glsl(${CMAKE_BINARY_DIR}/shaders processed DEFINES ${PP_DEFINES})
	set(blobs)
	foreach(pp ${processed})
		get_filename_component(name ${pp} NAME_WE)
		repgame_blob(blob src_shaders ${name} glsl ${pp})
		list(APPEND blobs ${blob})
	endforeach()
	set(${out_blobs} ${blobs} PARENT_SCOPE)
	set(${out_processed} ${processed} PARENT_SCOPE)
endfunction()
