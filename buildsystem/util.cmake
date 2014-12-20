# Copyright 2014-2014 the openage authors. See copying.md for legal info.

set(AUTOGEN_WARNING "warning: auto-generated file by cmake. look at the template file instead.")

function(print_filename filename)
	file(RELATIVE_PATH relfilename "${CMAKE_SOURCE_DIR}" "${filename}")
	message("${ARGN}\t${relfilename}")
endfunction()

function(list_contains var value)
	set(${var} FALSE PARENT_SCOPE)
	foreach(value2 ${ARGN})
		if(${value} STREQUAL ${value2})
			set(${var} TRUE PARENT_SCOPE)
		endif()
	endforeach()
endfunction()

function(disallow_in_source_builds)
	if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
		if(ALLOW_IN_SOURCE_BUILD)
			message("in-source build")
		else()
			message("


In-source builds are disallowed. They are a source of infinite pain,
- cluttering up the source folder
- causing cmake to silently refuse creating out-of-source builds
- overwriting the root Makefile (which contains cleaninsourcebuild)

To perform an out-of-source build,
- mkdir bin; cd bin; cmake ..
- or use ./configure, which will do this automatically

If you really want to perform an in-source build, use -DALLOW_IN_SOURCE_BUILD=TRUE

You need to run 'make cleaninsourcebuild' right now, since this (failed)
attempt already produced traces of an in-source build.
cmake will attempt in-source-builds instead of regular ones until you clean
those traces (remove CMakeCache.txt and CMakeFiles).



")
			message(FATAL_ERROR "aborting")
		endif()
	endif()
endfunction()
