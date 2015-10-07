MESSAGE("MSVC=${MSVC}")
IF(MSVC)
SET(COMPILE_FLAGS_PGO_INSTRUMENT "/GL")
SET(EXE_LINKER_FLAGS_PGO_INSTRUMENT  "/LTCG:PGI" )
SET(SHARED_LINKER_FLAGS_PGO_INSTRUMENT "/LTCG:PGI")
SET(STATIC_LIBRARY_FLAGS_PGO_INSTRUMENT  "/LTCG:NOSTATUS" )
SET(COMPILE_FLAGS_PGO_OPTIMIZE "/GL")
SET(EXE_LINKER_FLAGS_PGO_OPTIMIZE "/LTCG:PGU")
SET(SHARED_LINKER_FLAGS_PGO_OPTIMIZE "/LTCG:PGU" )
SET(STATIC_LIBRARY_FLAGS_PGO_OPTIMIZE "/LTCG:NOSTATUS")
ENDIF()

SET(PGO_TARGETS "mysys;strings;sql;xtradb;mysqld")

IF(PGO_TARGETS EQUAL "ALL")
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMPILE_FLAGS_PGO_${PGO_MODE}}")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILE_FLAGS_PGO_${PGO_MODE}}")
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EXE_LINKER_FLAGS_${PGO_MODE}}")
  SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${EXE_LINKER_FLAGS_${PGO_MODE}}")
  SET(CMAKE_STATIC_LINKER_FLAGS "${STATIC_LINKER_FLAGS} ${STATIC_LINKER_FLAGS_${PGO_MODE}}")
ENDIF()

FUNCTION(SET_PGO_TARGET_FLAGS  target)
  IF(NOT PGO_MODE)
    RETURN() 
  ENDIF()
  list (FIND PGO_TARGETS ${target} _index)
  if (${_index} EQUAL -1) 
    IF(NOT PGO_TARGETS EQUAL "ALL")
	  RETURN()
	ENDIF()
  ENDIF()
  IF(NOT PGO_MODE MATCHES "INSTRUMENT" AND NOT PGO_MODE MATCHES "OPTIMIZE")
    MESSAGE(FATAL_ERROR "Invalid PGO mode")
  ENDIF()
      get_target_property(target_type ${target} TYPE)
	  MESSAGE(STATUS "PGO ${PGO_MODE} , target ${target},type ${target_type}")
      message(STATUS "set_target_properties(${target} PROPERTIES COMPILE_FLAGS ${COMPILE_FLAGS_PGO_${PGO_MODE}})" )
	  
	  set_target_properties(${target} PROPERTIES COMPILE_FLAGS ${COMPILE_FLAGS_PGO_${PGO_MODE}})  
	  GET_TARGET_PROPERTY(old_link_flags  ${target} LINK_FLAGS)
	  IF(NOT old_link_flags)
	    SET(old_link_flags "")
	  ENDIF()
	  if(target_type MATCHES "EXECUTABLE")
	   
	   set_target_properties(${target} PROPERTIES LINK_FLAGS "${old_link_flags} ${EXE_LINKER_FLAGS_PGO_${PGO_MODE}}")
	   MESSAGE(STATUS "${target} PROPERTIES LINK_FLAGS ${old_link_flags} ${EXE_LINKER_FLAGS_PGO_${PGO_MODE}}")
	  elseif(type MATCHES "SHARED")
	   set_target_properties(${target} PROPERTIES LINK_FLAGS "${old_link_flags} ${SHARED_LINKER_FLAGS_PGO_${PGO_MODE}}")
	  else()
		set_target_properties(${target} PROPERTIES STATIC_LIBRARY_FLAGS ${STATIC_LIBRARY_FLAGS_PGO_${PGO_MODE}})

	  endif()
ENDFUNCTION()

FUNCTION (SET_PGO_FLAGS)
  IF(PGO_TARGETS EQUAL "ALL")
    RETURN()
  ENDIF()
  FOREACH(target ${PGO_TARGETS})
  SET_PGO_TARGET_FLAGS(${target})
  ENDFOREACH()
ENDFUNCTION()


