// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#pragma once

#include <fstream>

#include "pugixml.hpp"
#include "xr/xrp.h"

#ifdef XR_USE_PLATFORM_ANDROID
	std::string AndroidGetDataPath();
#endif

bool GetConfigurationFile(pugi::xml_document& out_config);

std::string StripIllegalFilenameCharacters(const std::string& str, const std::string& replace_with = "");