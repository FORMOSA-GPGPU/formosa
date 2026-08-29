
###############################################
###          build_source_group_include     ###
###############################################
###
### Builds a source group from a set of files
### for nicer display in IDEs
###

function( build_source_group_include )
	file(GLOB_RECURSE files ${CMAKE_CURRENT_SOURCE_DIR}/include/*.* )
	list(REMOVE_ITEM files "CMakeLists.txt")
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "[include]" FILES ${files})
endfunction()