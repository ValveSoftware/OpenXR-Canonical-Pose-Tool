// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#pragma once

#include <map>
#include <vector>

#include "items/item.h"
#include "xr/xrp.h"

struct PoseActionInfo {
	std::string name;
	bool reference;
	std::vector<std::string> subaction_paths;
	std::string suggested_binding;
	std::string base;
};

struct PoseInfo {
	std::string action_name;
	std::string binding_path;
	std::string interaction_profile;
	std::string base;
	XrPosef pose;
};

struct PoseOutputInfo {
	bool check_symmetrical;
	bool is_orientation_symmetrical;
	bool is_position_symmetrical;

	std::vector<PoseInfo> pose_infos;
};



class PoseInput {
   public:
	explicit PoseInput(PoseActionInfo action_info);

	bool Init(const XrpContext& context, const XrActionSet& action_set, const std::shared_ptr<PoseInput>& base_pose);

	XrSpace GetActionSpace(const std::string& subaction_path);
	PoseActionInfo GetActionInfo();

	bool GetSuggestedBinding(const XrpContext& context, std::vector<XrActionSuggestedBinding>& out_suggested_bindings);

	bool GetPoseInfo(const XrpContext& context, PoseOutputInfo& out_pose_output_info);

	~PoseInput();

   private:
	PoseActionInfo action_info_;

	std::shared_ptr<PoseInput> base_pose_;

	XrAction pose_action_ = XR_NULL_HANDLE;

	//subaction, space
	std::map<std::string, XrSpace> action_spaces_;
};