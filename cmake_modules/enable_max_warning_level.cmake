# Enables highest warning level for MSVC / GCC

if (MSVC)
	# MSVC complains if a different warning level is appended
	if (CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
		string (REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	else()
		set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
	endif()
elseif (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()