set(UI_SRC_DIRS
  "."
  "fonts"
)

set(UI_INCLUDE_DIRS
  "."
  "fonts"
)

set(UI_REQUIRES
  lvgl
)

idf_component_register(
  SRC_DIRS ${UI_SRC_DIRS}
  INCLUDE_DIRS ${UI_INCLUDE_DIRS}
  REQUIRES ${UI_REQUIRES}
)
