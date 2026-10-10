# On-device inference engine — shared verbatim with the PC sim via ../../../common/llm
set(LLM ${CMAKE_CURRENT_LIST_DIR}/../../../common/llm)
idf_component_register(
    SRCS "${LLM}/q4_model.c" "${LLM}/word_tok.c" "${LLM}/advisor_core.c"
    INCLUDE_DIRS "${LLM}"
    REQUIRES esp_timer tankcore
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -ffast-math)
if(CONFIG_AQUA_PETS_NO_PIE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AQUA_PETS_NO_PIE)
endif()
