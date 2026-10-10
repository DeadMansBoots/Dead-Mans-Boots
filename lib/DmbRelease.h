/*
 * DmbRelease.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

// The name this release goes by beside its number (K, October 9th: "Straker Beta"), as the window titles and the launcher show it.
// DMB_VERSION_STRING comes from cmake_modules/VersionDefinition.cmake, which also names the release for the release job.
#define DMB_RELEASE_LABEL_TEXT "beta.6"
#ifndef DMB_RELEASE_TEXT
#define DMB_RELEASE_TEXT "Straker Beta (" DMB_VERSION_STRING "-" DMB_RELEASE_LABEL_TEXT ")"
#endif
