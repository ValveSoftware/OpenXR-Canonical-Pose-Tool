// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#include "util_file.h"

#include <regex>

#ifdef XR_USE_PLATFORM_ANDROID
extern android_app* gapp;
#endif

#ifdef XR_USE_PLATFORM_ANDROID
std::string AndroidGetDataPath() { return gapp->activity->externalDataPath; }
#endif

bool GetConfigurationFile(pugi::xml_document& out_config) {
	static pugi::xml_document config_file_xml;

	if (!config_file_xml.first_child().empty()) {
		out_config.reset(config_file_xml);
		return true;
	}

#ifdef XR_USE_PLATFORM_ANDROID
	std::string config_path = AndroidGetDataPath() + "/cpt_config.xml";
#else
	std::string config_path = "cpt_config.xml";
#endif

	if (!config_file_xml.load_file(config_path.c_str())) {
		return false;
	}

	out_config.reset(config_file_xml);

	return true;
}

std::string StripIllegalFilenameCharacters(const std::string& str, const std::string& replace_with) {
	std::regex invalid_filename_characters(R"(<|>|:|"|\/|\\|\||\?|\*)");
	return std::regex_replace(str, invalid_filename_characters, replace_with);
}