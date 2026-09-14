idf_component_register(
  SRC_DIRS "." "fonts"
  INCLUDE_DIRS "." "fonts"
  REQUIRES lvgl
)

target_compile_options(${COMPONENT_LIB} PRIVATE
  "-include${CMAKE_SOURCE_DIR}/main/app_actions.h"
)