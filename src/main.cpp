// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#include <memory>
#include <regex>

#include "pugixml.hpp"
#include "util/util_file.h"
#include "xr/xrp.h"

#define CNFG_IMPLEMENTATION
#define CNFGOGL
#include "rawdraw_sf.h"

void HandleKey(int keycode, int bDown) {}
void HandleButton(int x, int y, int button, int bDown) {}
void HandleMotion(int x, int y, int mask) {}
void HandleDestroy() {}
void HandleResume() {}
void HandleSuspend() {}

#include "items/inputs/inputs.h"

static void SaveItemSetXML(const XrpContext& context, ItemSetOutput& item_set_output) {
	pugi::xml_document config;
	GetConfigurationFile(config);

	std::string runtime_name = context.instance_properties.runtimeName;
	for (const auto& runtime_xpath_node : config.select_nodes("//runtime")) {
		pugi::xml_node runtime_node = runtime_xpath_node.node();

		const std::string matches = runtime_node.attribute("matches").value();
		const std::string name = runtime_node.attribute("name").value();
		if (matches.empty() || name.empty()) {
			continue;
		}

		std::regex runtime_match_regex(matches);
		if (std::regex_match(context.instance_properties.runtimeName, runtime_match_regex)) {
			runtime_name = name;

			break;
		}
	}

	std::string base_path;
#ifdef XR_USE_PLATFORM_ANDROID
	base_path = AndroidGetDataPath() + "/";
#endif

	std::string base_file_name = base_path + "cpt_" + runtime_name;

	for (ItemFile& item_file : item_set_output.output_files) {
		const std::string file_name = base_file_name + "-" + item_file.name + ".xml";

		if (!item_file.document.save_file(file_name.c_str())) {
			XrpLog("failed to save file: %s", file_name.c_str());
			continue;
		}
	}
}

void MakeFile(const std::vector<std::unique_ptr<IItemSet>>& item_sets, const XrpContext& context) {
	for (const auto& item_set : item_sets) {
		ItemSetOutput item_set_output;
		if (!item_set->GetOutput(context, item_set_output)) {
			XrpLog("failed to get item set, retrying next frame");
			return;
		};

		SaveItemSetXML(context, item_set_output);
	}

	// Exit the session as we're done
	XrpRequestExitSession(context);
}

static std::map<std::string, std::unique_ptr<IItemSet>> GetAllItemSets(const pugi::xml_node& config_node) {
	std::map<std::string, std::unique_ptr<IItemSet>> item_sets;
	item_sets["inputs"] = std::make_unique<InputItemSet>(config_node.child("inputs"));

	return item_sets;
}

int main(int argc, char* argv[]) {
	XrpContext context;

	{
		CNFGSetup("OpenXR Canonical Pose Tool", 600, 600);

		// Initialize the app
		XrpApp app = {
			.app_name = "Pose Checker",
			.app_version = 1,
			.engine_name = "danwillm",
			.engine_version = 1,
		};

		pugi::xml_document config_doc;
		if (!GetConfigurationFile(config_doc)) {
			XrpLog("Failed to parse configuration!");
			return -1;
		}

		pugi::xml_node config_node = config_doc.child("canonical_pose_tool");

		std::vector<std::unique_ptr<IItemSet>> enabled_item_sets{};
		{
			std::map<std::string, std::unique_ptr<IItemSet>> all_item_sets = GetAllItemSets(config_node);

			for (const pugi::xpath_node enabled_item_node : config_node.select_nodes("./output/item")) {
				const std::string enabled_item = enabled_item_node.node().text().get();

				if (!all_item_sets.contains(enabled_item)) {
					XrpLog("Unknown or already specified item: %s. Skipping", enabled_item.c_str());
					continue;
				}

				enabled_item_sets.emplace_back(std::move(all_item_sets[enabled_item]));
				all_item_sets.erase(enabled_item);
			}
		}

		for (const auto& item_set : enabled_item_sets) {
			std::set<std::string> required_extensions;
			if (!item_set->GetRequiredExtensions(required_extensions)) {
				XrpLog("Could not get requested extensions from item set");

				return -1;
			}

			app.requested_extensions.insert(required_extensions.begin(), required_extensions.end());
		}

		if (!XrpInit(app, context)) {
			XrpLog("Failed to initialize xr");

			return -1;
		}

		if (!XrpRunFrameLoop(context, [&](XrpEvent event, XrpEventData event_data) {
				switch (event) {
					case XRP_EVENT_SESSION_READY: {
						// Initialize the item sets
						for (const auto& item_set : enabled_item_sets) {
							if (!item_set->Init(context)) {
								XrpLog("Failed to initialize item set");

								return false;
							}
						}
						break;
					}

					case XRP_EVENT_DO_FRAME: {
						if (event_data.session_state != XR_SESSION_STATE_FOCUSED) {
							XrpLog("Session not focused");
							break;
						}

						MakeFile(enabled_item_sets, context);
						break;
					}

					default:
						break;
				}

				return true;
			})) {
			XrpLog("run frame loop failed!");
		}
	}

	XrpDestroy(context);

	return 0;
}
