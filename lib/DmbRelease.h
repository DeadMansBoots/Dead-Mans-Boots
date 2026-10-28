/*
 * DmbRelease.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

// The name this release goes by beside its number (the project owner, October 9th: "Straker Beta"), as the window titles and the launcher show it.
// DMB_VERSION_STRING comes from cmake_modules/VersionDefinition.cmake, which also names the release for the release job.
#define DMB_RELEASE_LABEL_TEXT "beta.16"
// DMB_BUILD_ID: the commit a candidate was built from, written to lib/DmbBuildId.h (not in git) by the build chain; a build with no such file reads "dev"
#if defined(__has_include)
#if __has_include("DmbBuildId.h")
#include "DmbBuildId.h"
#endif
#endif
#ifndef DMB_BUILD_ID
#define DMB_BUILD_ID "dev"
#endif
// The update check compares DMB_VERSION_STRING and DMB_RELEASE_LABEL_TEXT (launcher/updatedialog_moc.cpp); what a player reads is this text. Beta 16, the
// stable release for the week of October 24th (the project owner's order); a candidate after it puts its build id back in this text (DMB_BUILD_ID), as the beta 16 candidates did.
#ifndef DMB_RELEASE_TEXT
#define DMB_RELEASE_TEXT "Straker Beta (" DMB_VERSION_STRING "-" DMB_RELEASE_LABEL_TEXT ")"
#endif
