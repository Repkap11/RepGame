# CUDA/HIP GPU objects via add_custom_command around nvcc/hipcc — mirrors
# makefiles/cuda.mk and hip.mk. enable_language(CUDA/HIP) comes in a later
# phase. Buildable standalone (CLIENT=OFF SERVER=OFF) for the docker
# gpu-prebuilt path: objects land under ${REPGAME_GPU_OUT}/linux/{cuda,hip}
# so `make gpu-prebuilt` keeps producing the layout the flatpak consumes.

file(GLOB_RECURSE REPGAME_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/include/*.hpp)

set(REPGAME_NVCC /usr/bin/nvcc)
set(REPGAME_HIPCC /usr/bin/hipcc)

if(REPGAME_CUDA AND EXISTS ${REPGAME_NVCC})
	set(REPGAME_CUDA_BUILT 1)
else()
	set(REPGAME_CUDA_BUILT 0)
endif()
if(REPGAME_HIP AND EXISTS ${REPGAME_HIPCC})
	set(REPGAME_HIP_BUILT 1)
else()
	set(REPGAME_HIP_BUILT 0)
endif()

if(CMAKE_CXX_COMPILER_LAUNCHER)
	set(_gpu_launcher ${CMAKE_CXX_COMPILER_LAUNCHER})
else()
	set(_gpu_launcher)
endif()

# ---- CUDA ----------------------------------------------------------------
set(REPGAME_CUDA_LINK_OBJECTS)
if(REPGAME_CUDA_BUILT)
	set(_cuda_dir ${REPGAME_GPU_OUT}/linux/cuda)
	set(_cuda_flags
		-Xcompiler -Wall
		-Xcompiler -Wextra
		-Xcompiler -Werror
		-Xcompiler -Wno-unused-parameter
		-Xcompiler -Wno-unused-variable
		-arch=sm_50
		-gencode=arch=compute_50,code=sm_50
		-gencode=arch=compute_52,code=sm_52
		-gencode=arch=compute_60,code=sm_60
		-gencode=arch=compute_61,code=sm_61
		-gencode=arch=compute_70,code=sm_70
		-gencode=arch=compute_75,code=sm_75
		-gencode=arch=compute_75,code=compute_75
		-Xcompiler -fPIC -DREPGAME_LINUX)
	set(_cuda_includes
		-I${CMAKE_SOURCE_DIR}/include/
		-I${CMAKE_SOURCE_DIR}/imgui_build
		-I${CMAKE_SOURCE_DIR}/imgui_build/backends
		-isystem ${CMAKE_SOURCE_DIR}/entt_build/src)
	file(GLOB _cuda_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/linux/cuda/*.cu)
	set(_cuda_objects)
	foreach(src ${_cuda_srcs})
		get_filename_component(name ${src} NAME_WE)
		set(obj ${_cuda_dir}/${name}.o)
		add_custom_command(OUTPUT ${obj}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${_cuda_dir}
			COMMAND ${_gpu_launcher} ${REPGAME_NVCC} -x cu -dc -c ${_cuda_includes} ${_cuda_flags} ${src} -o ${obj}
			DEPENDS ${src} ${REPGAME_HEADERS}
			COMMENT "nvcc ${name}.cu"
			VERBATIM
			COMMAND_EXPAND_LISTS)
		list(APPEND _cuda_objects ${obj})
	endforeach()
	set(_cuda_device ${_cuda_dir}/libRepGame_device.o)
	set(_cuda_lib ${_cuda_dir}/libRepGame.o)
	add_custom_command(OUTPUT ${_cuda_device}
		COMMAND ${_gpu_launcher} ${REPGAME_NVCC} ${_cuda_flags} --lib ${_cuda_objects} -o ${_cuda_device}
		DEPENDS ${_cuda_objects}
		COMMENT "nvcc --lib libRepGame_device.o"
		VERBATIM
		COMMAND_EXPAND_LISTS)
	add_custom_command(OUTPUT ${_cuda_lib}
		COMMAND ${_gpu_launcher} ${REPGAME_NVCC} ${_cuda_flags} -dlink ${_cuda_device} -o ${_cuda_lib}
		DEPENDS ${_cuda_device}
		COMMENT "nvcc -dlink libRepGame.o"
		VERBATIM
		COMMAND_EXPAND_LISTS)
	set(REPGAME_CUDA_LINK_OBJECTS ${_cuda_lib} ${_cuda_device})
	add_custom_target(repgame_cuda DEPENDS ${REPGAME_CUDA_LINK_OBJECTS})
else()
	add_custom_target(repgame_cuda
		COMMAND ${CMAKE_COMMAND} -E echo "repgame_cuda: nvcc not found or REPGAME_CUDA=OFF")
endif()

# ---- HIP -----------------------------------------------------------------
set(REPGAME_HIP_LINK_OBJECTS)
if(REPGAME_HIP_BUILT)
	set(_hip_dir ${REPGAME_GPU_OUT}/linux/hip)
	# Generic code objects target an entire GPU family with one arch, so a
	# single binary runs on every supported AMD GPU (including future ones
	# within each family) without listing each chip individually. Requires
	# Code Object V6 (ROCm 6.0+); gfx12-generic / gfx1151 need a newer ROCm
	# on the build machine.
	set(_hip_flags
		-Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable
		-Wno-c++20-designator
		--offload-arch=gfx9-generic
		--offload-arch=gfx10-1-generic
		--offload-arch=gfx10-3-generic
		--offload-arch=gfx11-generic
		--offload-arch=gfx12-generic
		-fPIC -DREPGAME_LINUX)
	set(_hip_includes
		-I${CMAKE_SOURCE_DIR}/include/
		-I${CMAKE_SOURCE_DIR}/imgui_build
		-I${CMAKE_SOURCE_DIR}/imgui_build/backends
		-isystem ${CMAKE_SOURCE_DIR}/entt_build/src)
	file(GLOB _hip_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/linux/hip/*.hip)
	set(_hip_objects)
	foreach(src ${_hip_srcs})
		get_filename_component(name ${src} NAME_WE)
		set(obj ${_hip_dir}/${name}.o)
		add_custom_command(OUTPUT ${obj}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${_hip_dir}
			COMMAND ${_gpu_launcher} ${REPGAME_HIPCC} -x hip -c -fno-rtti ${_hip_includes} ${_hip_flags} ${src} -o ${obj}
			DEPENDS ${src} ${REPGAME_HEADERS}
			COMMENT "hipcc ${name}.hip"
			VERBATIM
			COMMAND_EXPAND_LISTS)
		list(APPEND _hip_objects ${obj})
	endforeach()
	set(REPGAME_HIP_LINK_OBJECTS ${_hip_objects})
	add_custom_target(repgame_hip DEPENDS ${REPGAME_HIP_LINK_OBJECTS})
else()
	add_custom_target(repgame_hip
		COMMAND ${CMAKE_COMMAND} -E echo "repgame_hip: hipcc not found or REPGAME_HIP=OFF")
endif()

# repgame_gpu_link(<target>): add GPU objects, defines and runtime libs —
# freshly built when a toolchain is present, otherwise the prebuilt objects
# from REPGAME_GPU_PREBUILT_DIR (same else-branch flags as the old makefiles).
function(repgame_gpu_link target)
	if(REPGAME_CUDA_BUILT)
		target_sources(${target} PRIVATE ${REPGAME_CUDA_LINK_OBJECTS})
		target_compile_definitions(${target} PRIVATE REPGAME_BUILD_WITH_CUDA)
		target_link_libraries(${target} PRIVATE -lcudart_static)
	# No CUDA toolchain (e.g. the flatpak sandbox): link objects prebuilt in
	# the Docker image by `make gpu-prebuilt` (makefiles/docker.mk). cudart
	# is static, so only libcudart_static.a is needed from
	# $(GPU_PREBUILT_DIR)/lib at link time.
	elseif(NOT "${REPGAME_GPU_PREBUILT_DIR}" STREQUAL "" AND
			EXISTS ${REPGAME_GPU_PREBUILT_DIR}/linux/cuda/libRepGame.o)
		target_sources(${target} PRIVATE
			${REPGAME_GPU_PREBUILT_DIR}/linux/cuda/libRepGame.o
			${REPGAME_GPU_PREBUILT_DIR}/linux/cuda/libRepGame_device.o)
		target_compile_definitions(${target} PRIVATE REPGAME_BUILD_WITH_CUDA)
		target_link_libraries(${target} PRIVATE
			-L${REPGAME_GPU_PREBUILT_DIR}/lib
			-Wl,-rpath-link,${REPGAME_GPU_PREBUILT_DIR}/lib
			-lcudart_static)
	endif()

	if(REPGAME_HIP_BUILT)
		target_sources(${target} PRIVATE ${REPGAME_HIP_LINK_OBJECTS})
		target_compile_definitions(${target} PRIVATE REPGAME_BUILD_WITH_HIP)
		target_link_libraries(${target} PRIVATE -lamdhip64)
	# No HIP toolchain (e.g. the flatpak sandbox): link objects prebuilt in
	# the Docker image by `make gpu-prebuilt`. The ROCm runtime libs they
	# need ship in $(GPU_PREBUILT_DIR)/lib; -rpath-link lets ld resolve their
	# transitive deps without installing them.
	elseif(NOT "${REPGAME_GPU_PREBUILT_DIR}" STREQUAL "")
		file(GLOB _hip_prebuilt ${REPGAME_GPU_PREBUILT_DIR}/linux/hip/*.o)
		if(_hip_prebuilt)
			target_sources(${target} PRIVATE ${_hip_prebuilt})
			target_compile_definitions(${target} PRIVATE REPGAME_BUILD_WITH_HIP)
			target_link_libraries(${target} PRIVATE
				-L${REPGAME_GPU_PREBUILT_DIR}/lib
				-Wl,-rpath-link,${REPGAME_GPU_PREBUILT_DIR}/lib
				-lamdhip64)
		endif()
	endif()
endfunction()
