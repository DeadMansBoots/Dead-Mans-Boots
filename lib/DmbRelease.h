/*
 * DmbRelease.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

// The name this release goes by beside its number (K, October 9th: "Straker Beta"), as the window titles and the launcher show it.
// DMB_VERSION_STRING comes from cmake_modules/VersionDefinition.cmake, which also names the release for the release job.
#define DMB_RELEASE_LABEL_TEXT "beta.15"
// DMB_BUILD_ID: the commit a candidate was built from, written to lib/DmbBuildId.h (not in git) by the build chain; a build with no such file reads "dev"
#if defined(__has_include)
#if __has_include("DmbBuildId.h")
#include "DmbBuildId.h"
#endif
#endif
#ifndef DMB_BUILD_ID
#define DMB_BUILD_ID "dev"
#endif
// The update check compares DMB_VERSION_STRING and DMB_RELEASE_LABEL_TEXT (launcher/updatedialog_moc.cpp), so the label stays beta.15; what a player reads is this text.
// The candidate on top of beta 15 (K, October 19th and 22nd, "latest version of dmb tonight"): the beta 16 candidate, with the build id, so two builds can be told apart.
#ifndef DMB_RELEASE_TEXT
#define DMB_RELEASE_TEXT "Straker Beta (" DMB_VERSION_STRING "-beta.16 candidate, build " DMB_BUILD_ID ")"
#endif
