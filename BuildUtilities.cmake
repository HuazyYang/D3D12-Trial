# copy asset file from source directory to binary directory.
function(copy_assets asset_files dir_name copied_files)
foreach(asset ${${asset_files}})
  #message("asset: ${asset}")
  get_filename_component(file_name ${asset} NAME)
  get_filename_component(full_path ${asset} ABSOLUTE)
  set(output_dir ${CMAKE_CURRENT_BINARY_DIR}/${TRIAL_OUTDIR_SUFFIX}/${dir_name})
  set(output_file ${output_dir}/${file_name})
  set(${copied_files} ${${copied_files}} ${output_file})
  set(${copied_files} ${${copied_files}} PARENT_SCOPE)
  set_source_files_properties(${asset} PROPERTIES HEADER_FILE_ONLY TRUE)
  if (WIN32)
    add_custom_command(
      OUTPUT ${output_file}
      #COMMAND mklink \"${output_file}\" \"${full_path}\"
      COMMAND xcopy \"${full_path}\" \"${output_file}*\" /Y /Q /F
      DEPENDS ${full_path}
    )
  else()
    add_custom_command(
      OUTPUT ${output_file}
      COMMAND mkdir --parents ${output_dir} && cp --force --link \"${full_path}\" \"${output_file}\"
      DEPENDS ${full_path}
    )
  endif()
endforeach()
endfunction()

set(DXC_FLAGS  /nologo /WX /Ges /Zi /Zpc /Qstrip_reflect /Qstrip_debug)

# HLSL shader compilation
function(compile_shader_dxil shader_file outdir_name target_stage entry_point compiled_shaders)
  get_filename_component(file_name ${shader_file} NAME_WE)
  get_filename_component(full_path ${shader_file} ABSOLUTE)
  set(output_dir ${outdir_name})
  set(output_file ${output_dir}/${file_name}_${entry_point}.hlsl.inc)
  set(output_pdb_file ${output_dir}/${file_name}_${entry_point}.hlsl.pdb)
  set(${compiled_shaders} ${compiled_shaders} ${output_file})
  set(${compiled_shaders} ${${compiled_shaders}} PARENT_SCOPE)
  set_source_files_properties(${shader_file} PROPERTIES HEADER_FILE_ONLY TRUE)
  set_source_files_properties(${shader_file} PROPERTIES VS_TOOL_OVERRIDE None)
  if (WIN32)
        add_custom_command(
            OUTPUT ${output_file}
            COMMAND ${DXC_COMPILER} ${full_path} ${DXC_FLAGS} /T${target_stage}_${HLSL_SHADER_MODEL_VERSION} /E${entry_point} /Fh${output_file} /Fd${output_pdb_file}
            /Vn${file_name}_${entry_point}
            DEPENDS ${full_path}
        )
    else()
        add_custom_command(
            OUTPUT ${output_file}
            COMMAND mkdir --parents ${output_dir} && ${DXC_COMPILER} ${DXC_FLAGS} /T${target_stage}_${HLSL_SHADER_MODEL_VERSION} /E${entry_point} /Fh${output_file}
              /Fd${output_pdb_file} /Vn${file_name}_${entry_point} ${full_path}
            DEPENDS ${full_path}
        )
    endif()
endfunction()
