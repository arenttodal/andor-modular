# ANDOR Modular, the plugin (VST3 / AU / Standalone) and its product tests.
#
# The product's sources compile into the plugin and into modular_product_tests (a console
# program: processor, state, editor, Quick Add and screenshots, run headless under a virtual
# display). JUCE modules are compiled per target with that target's configuration.

set(ANDOR_UI_FONTS ${MODULAR_OSP_DIR}/shared/andor-ui/Resources/fonts)
file(GLOB MODULAR_FACTORY_PATCHES ${PROJECT_SOURCE_DIR}/presets/factory/*.andormodular)
file(GLOB MODULAR_FACTORY_MODULES ${PROJECT_SOURCE_DIR}/presets/modules/*.andormodule)
file(GLOB MODULAR_FACTORY_SAMPLES ${PROJECT_SOURCE_DIR}/presets/factory/samples/*.wav)

# Typefaces: ANDOR's own selection (SIL Open Font License, texts in resources/licenses) and the
# factory content (patches, modules, ANDOR's synthesised recordings).
juce_add_binary_data(ModularData
    HEADER_NAME ModularData.h
    NAMESPACE ModularData
    SOURCES
        ${ANDOR_UI_FONTS}/Inter-Regular.ttf
        ${ANDOR_UI_FONTS}/Inter-Medium.ttf
        ${ANDOR_UI_FONTS}/Inter-SemiBold.ttf
        ${ANDOR_UI_FONTS}/Outfit-ExtraLight.ttf
        ${ANDOR_UI_FONTS}/Outfit-ExtraBold.ttf
        ${MODULAR_FACTORY_PATCHES}
        ${MODULAR_FACTORY_MODULES}
        ${MODULAR_FACTORY_SAMPLES})

set(MODULAR_PRODUCT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ExternalHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Canvas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/QuickAdd.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DetailPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PlayView.cpp)

set(MODULAR_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
    DONT_SET_USING_JUCE_NAMESPACE=1
    MODULAR_PRODUCT_NAME="${MODULAR_NAME}")

if(APPLE)
    set(MODULAR_FORMATS AU VST3 Standalone)
    list(APPEND MODULAR_JUCE_DEFINITIONS JUCE_PLUGINHOST_AU=1)
else()
    set(MODULAR_FORMATS VST3 Standalone)
endif()

juce_add_plugin(ANDOR_Modular
    PRODUCT_NAME "${MODULAR_NAME}"
    VERSION ${MODULAR_VERSION}
    COMPANY_NAME "OSP"
    BUNDLE_ID "${MODULAR_BUNDLE}"
    PLUGIN_MANUFACTURER_CODE Ospx
    PLUGIN_CODE ${MODULAR_CODE}
    FORMATS ${MODULAR_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(ANDOR_Modular PRIVATE ${MODULAR_PRODUCT_SOURCES})
target_include_directories(ANDOR_Modular PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(ANDOR_Modular PUBLIC ${MODULAR_JUCE_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=0 JucePlugin_Build_Product=1)
target_link_libraries(ANDOR_Modular
    PRIVATE
        andor_modular_dsp
        ModularData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Product tests: the processor in a host-like loop, state recall, the editor, Quick Add,
# smart edits through the UI layer, and screenshots (MODULAR_SCREENSHOT_DIR).
# ---------------------------------------------------------------------------
if(MODULAR_BUILD_TESTS)
    juce_add_console_app(modular_product_tests PRODUCT_NAME "modular_product_tests")
    target_sources(modular_product_tests PRIVATE
        ${MODULAR_PRODUCT_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ProductTests.cpp)
    target_include_directories(modular_product_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_definitions(modular_product_tests PRIVATE ${MODULAR_JUCE_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="ANDOR Modular" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0 JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_VersionString="${MODULAR_VERSION}")
    target_link_libraries(modular_product_tests PRIVATE
        andor_modular_dsp
        ModularData
        Catch2::Catch2
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_recommended_config_flags)
    add_test(NAME modular_product_tests COMMAND modular_product_tests)
endif()
