set(VCMI_VERSION_MAJOR 1)
set(VCMI_VERSION_MINOR 7)
set(VCMI_VERSION_PATCH 5)
add_definitions(
	-DVCMI_VERSION_MAJOR=${VCMI_VERSION_MAJOR}
	-DVCMI_VERSION_MINOR=${VCMI_VERSION_MINOR}
	-DVCMI_VERSION_PATCH=${VCMI_VERSION_PATCH}
	-DVCMI_VERSION_STRING="${VCMI_VERSION_MAJOR}.${VCMI_VERSION_MINOR}.${VCMI_VERSION_PATCH}"
)
# Dead Man's Boots' own release number, beside the VCMI version it is built on. The launcher's
# update check and the window titles use it; saves, mods and bug reports keep VCMI's.
set(DMB_VERSION "0.1.0")
add_definitions(-DDMB_VERSION_STRING="${DMB_VERSION}")
# The name this release goes by (K, October 9th: "Straker Beta"). The release job reads it for the installer's file name and the release's title;
# the text the windows show is lib/DmbRelease.h. The number above stays the one the AI plugins' builtFor check, the update notice and the tag use.
set(DMB_RELEASE_NAME "Straker Beta")
set(DMB_RELEASE_LABEL "beta.8")
# The program properties' file version (win/WindowsFileInfo.rc.in reads it as a CMake variable)
set(VCMI_VERSION_STRING "${VCMI_VERSION_MAJOR}.${VCMI_VERSION_MINOR}.${VCMI_VERSION_PATCH}")
set(APP_SHORT_VERSION "${VCMI_VERSION_MAJOR}.${VCMI_VERSION_MINOR}")
if(NOT VCMI_VERSION_PATCH EQUAL 0)
	string(APPEND APP_SHORT_VERSION ".${VCMI_VERSION_PATCH}")
endif()
