############################################################################
# CMakeLists.txt file for building ROOT io/rfio package
############################################################################

include_directories(${CASTOR_INCLUDE_DIR})

ROOT_GENERATE_DICTIONARY(G__RFIO *.h MODULE RFIO  LINKDEF LinkDef.h)

ROOT_ADD_CXX_FLAG(CMAKE_CXX_FLAGS -Wno-shadow)

ROOT_LINKER_LIBRARY(RFIO *.cxx G__RFIO.cxx LIBRARIES Core ${CASTOR_LIBRARIES}
    ${CASTOR_rfio_LIBRARY} ${CASTOR_common_LIBRARY} ${CASTOR_client_LIBRARY} DEPENDENCIES Net RIO)
ROOT_INSTALL_HEADERS()
