// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#include "inputs.h"

#include <thread>
#include <utility>

#include "action_pose.h"
#include "util/util_file.h"
#include "xr/xrp.h"

template <typename... Args>
std::string string_format(const std::string &format, Args... args) {
	int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
	if (size_s <= 0) {
		throw std::runtime_error("Error during formatting.");
	}
	auto size = static_cast<size_t>(size_s);
	std::unique_ptr<char[]> buf(new char[size]);
	std::snprintf(buf.get(), size, format.c_str(), args...);
	return {buf.get(), buf.get() + size - 1};
}

InputItemSet::InputItemSet(pugi::xml_node inputs_config) { config_ = inputs_config; }

bool InputItemSet::GetRequiredExtensions(std::set<std::string> &out_extensions) {
	// some interaction profiles require extensions
	for (const pugi::xpath_node &interaction_profile_xpath_node : config_.select_nodes("./interaction_profiles/interaction_profile")) {
		const pugi::xml_attribute requires_extension_attribute = interaction_profile_xpath_node.node().attribute("requires_extension");
		if (requires_extension_attribute.empty()) {
			continue;
		}

		out_extensions.emplace(requires_extension_attribute.value());
	}

	// some actions require extensions (e.g. XR_EXT_palm_pose)
	for (const pugi::xpath_node &action_xpath_node : config_.select_nodes("./actions/action")) {
		const pugi::xml_attribute requires_extension_attribute = action_xpath_node.node().attribute("requires_extension");
		if (requires_extension_attribute.empty()) {
			continue;
		}

		out_extensions.emplace(requires_extension_attribute.value());
	}

	return true;
}

bool InputItemSet::Init(const XrpContext &context) {
	XrActionSetCreateInfo action_set_create_info = {
		.type = XR_TYPE_ACTION_SET_CREATE_INFO,
		.next = nullptr,
		.actionSetName = "default",
		.localizedActionSetName = "default",
		.priority = 5,
	};

	XRP_CHECK_OR_RETURN(context, xrCreateActionSet(context.instance, &action_set_create_info, &action_set_));

	for (const pugi::xpath_node &action_xpath_node : config_.select_nodes("./actions/action")) {
		const pugi::xml_node action_node = action_xpath_node.node();

		if (std::string(action_node.attribute("type").value()) == "pose") {
			std::vector<std::string> subaction_paths{};
			for (const pugi::xpath_node &subaction_node : action_node.select_nodes("./subaction_path")) {
				subaction_paths.emplace_back(subaction_node.node().text().get());
			}

			const std::string action_name = action_node.attribute("name").value();
			const std::string suggested_binding = action_node.attribute("suggested_binding").value();
			const std::string base = action_node.attribute("base").value();
			const bool is_reference_pose = action_node.attribute("reference").as_bool();

			if (action_name.empty() || suggested_binding.empty()) {
				XrpLog("Skipping action because action name or suggested binding was empty");
				continue;
			}

			PoseActionInfo pose_info = {
				.name = action_name,
				.reference = is_reference_pose,
				.subaction_paths = subaction_paths,
				.suggested_binding = suggested_binding,
				.base = base,
			};
			poses_[action_name] = std::make_shared<PoseInput>(pose_info);

		} else {
			XrpLog("Unknown or unsupported action type");
		}
	}

	std::vector<XrActionSuggestedBinding> suggested_bindings;
	for (const auto &pose : poses_) {
		if (!pose.second->Init(context, action_set_, poses_[pose.second->GetActionInfo().base])) {
			XrpLog("failed to create input");

			return false;
		}

		std::vector<XrActionSuggestedBinding> action_suggested_bindings;
		if (!pose.second->GetSuggestedBinding(context, action_suggested_bindings)) {
			XrpLog("Unable to get suggested bindings for input. Skipping");

			continue;
		}
		suggested_bindings.insert(suggested_bindings.end(), action_suggested_bindings.begin(), action_suggested_bindings.end());
	}

	for (const pugi::xpath_node &interaction_profile_xpath_node : config_.select_nodes("./interaction_profiles/interaction_profile")) {
		const std::string interaction_profile_string = interaction_profile_xpath_node.node().text().get();
		XrPath interaction_profile_path = XrpStringToXrPath(context, interaction_profile_string);

		XrInteractionProfileSuggestedBinding suggested_bindings_info = {
			.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
			.next = nullptr,
			.interactionProfile = interaction_profile_path,
			.countSuggestedBindings = static_cast<uint32_t>(suggested_bindings.size()),
			.suggestedBindings = suggested_bindings.data(),
		};

		const XrResult result = xrSuggestInteractionProfileBindings(context.instance, &suggested_bindings_info);
		if (result == XR_ERROR_PATH_UNSUPPORTED) {
			XrpLog("interaction profile did not support suggested bindings");
			continue;
		}

		XRP_CHECK_OR_RETURN(context, result);

		XrpLog("set interaction profile for: %s", interaction_profile_string.c_str());
	}

	XrSessionActionSetsAttachInfo attach_info = {
		.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
		.next = nullptr,
		.countActionSets = 1,
		.actionSets = &action_set_,
	};
	XRP_CHECK_OR_RETURN(context, xrAttachSessionActionSets(context.session, &attach_info));

	return true;
}

static bool LoadReferenceXMLDocument(const XrpContext &context, const std::string &interaction_profile, pugi::xml_document &out_document) {
	pugi::xml_document config;
	GetConfigurationFile(config);

	for (const pugi::xpath_node &runtime_input_xpath_node : config.select_nodes("//runtime/canonical_items/inputs/input")) {
		const pugi::xml_node runtime_section_node = runtime_input_xpath_node.node();

		if (runtime_section_node.attribute("interaction_profile").value() == interaction_profile) {
			out_document.load_file(runtime_section_node.text().get());
			return true;
		}
	}

	return false;
}

static void MakeNode(pugi::xml_node &node, const std::string &node_name, const std::string &node_value) {
	pugi::xml_node child_node = node.append_child(node_name.c_str());
	child_node.append_child(pugi::node_pcdata).set_value(node_value.c_str());
}

bool InputItemSet::GetOutput(const XrpContext &context, ItemSetOutput &out_itemset) {
	XrActiveActionSet active_action_set = {
		.actionSet = action_set_,
		.subactionPath = XR_NULL_PATH,
	};

	XrActionsSyncInfo sync_info = {
		.type = XR_TYPE_ACTIONS_SYNC_INFO,
		.next = nullptr,
		.countActiveActionSets = 1,
		.activeActionSets = &active_action_set,
	};
	XRP_CHECK_OR_RETURN(context, xrSyncActions(context.session, &sync_info));

	// map interaction profiles to files
	std::map<std::string, ItemFile> interaction_profile_files;

	for (auto &pose : poses_) {
		if (pose.first.empty()) continue;

		PoseOutputInfo pose_output_info;
		if (!pose.second->GetPoseInfo(context, pose_output_info)) {
			XrpLog("Unable to get pose info.");
			return false;
		}

		for (const auto &pose_info : pose_output_info.pose_infos) {
			if (!interaction_profile_files.contains(pose_info.interaction_profile)) {
				{
					const std::string interaction_profile_unwanted = "/interaction_profiles/";
					std::string interaction_profile_safe = pose_info.interaction_profile;
					interaction_profile_safe.erase(interaction_profile_safe.find(interaction_profile_unwanted),
												   interaction_profile_unwanted.length());

					interaction_profile_files[pose_info.interaction_profile] = {
						.name = StripIllegalFilenameCharacters(interaction_profile_safe, "_"),
					};
				}
				pugi::xml_node inputs_node = interaction_profile_files[pose_info.interaction_profile].document.append_child("inputs");
				inputs_node.append_attribute("interaction_profile") = pose_info.interaction_profile.c_str();
			}

			pugi::xml_node inputs_node = interaction_profile_files[pose_info.interaction_profile].document.child("inputs");
			pugi::xml_node pose_node = inputs_node.append_child("pose");

			pose_node.append_attribute("name") = pose_info.action_name.c_str();
			pose_node.append_attribute("base") = pose_info.base.c_str();
			pose_node.append_attribute("binding_path") = pose_info.binding_path.c_str();

			pugi::xml_document reference_doc;
			LoadReferenceXMLDocument(context, pose_info.interaction_profile, reference_doc);

			const pugi::xml_node reference_pose_node =
				reference_doc
					.select_node(string_format("/inputs/pose[@name='%s' and @base='%s' and @binding_path='%s']", pose_info.action_name.c_str(),
											   pose_info.base.c_str(), pose_info.binding_path.c_str())
									 .c_str())
					.node();

			if (reference_pose_node.empty()) {
				XrpLog("Could not find canonical pose: %s in reference file for interaction profile: %s", pose_info.action_name.c_str(),
					   pose_info.interaction_profile.c_str());
			}

			{
				pugi::xml_node position_node = pose_node.append_child("position");
				const pugi::xml_node reference_position_node = reference_pose_node.child("position");

				position_node.append_attribute("unit") = "meters";
				if (pose_output_info.check_symmetrical) {
					position_node.append_attribute("symmetrical") = pose_output_info.is_position_symmetrical;
				}

				if (!reference_pose_node.empty()) {
					XrVector3f reference_position = {
						.x = reference_position_node.child("X").text().as_float(),
						.y = reference_position_node.child("Y").text().as_float(),
						.z = reference_position_node.child("Z").text().as_float(),
					};
					position_node.append_attribute("matches_canonical") = reference_position == pose_info.pose.position;
				}

				MakeNode(position_node, "X", XrpRoundFloatToString(pose_info.pose.position.x, 3));
				MakeNode(position_node, "Y", XrpRoundFloatToString(pose_info.pose.position.y, 3));
				MakeNode(position_node, "Z", XrpRoundFloatToString(pose_info.pose.position.z, 3));
			}
			{
				pugi::xml_node orientation_node = pose_node.append_child("orientation");
				const pugi::xml_node reference_orientation_node = reference_pose_node.child("orientation");

				if (pose_output_info.check_symmetrical) {
					orientation_node.append_attribute("symmetrical") = pose_output_info.is_orientation_symmetrical;
				}

				if (!reference_pose_node.empty()) {
					XrQuaternionf reference_orientation = {
						.x = reference_orientation_node.child("X").text().as_float(),
						.y = reference_orientation_node.child("Y").text().as_float(),
						.z = reference_orientation_node.child("Z").text().as_float(),
						.w = reference_orientation_node.child("W").text().as_float(),
					};
					orientation_node.append_attribute("matches_canonical") = reference_orientation == pose_info.pose.orientation;
				}

				MakeNode(orientation_node, "W", XrpRoundFloatToString(pose_info.pose.orientation.w, 2));
				MakeNode(orientation_node, "X", XrpRoundFloatToString(pose_info.pose.orientation.x, 2));
				MakeNode(orientation_node, "Y", XrpRoundFloatToString(pose_info.pose.orientation.y, 2));
				MakeNode(orientation_node, "Z", XrpRoundFloatToString(pose_info.pose.orientation.z, 2));
			}
		}
	}

	for (auto &interaction_profile_file : interaction_profile_files) {
		out_itemset.output_files.emplace_back(std::move(interaction_profile_file.second));
	}

	return true;
}

InputItemSet::~InputItemSet() { xrDestroyActionSet(action_set_); }