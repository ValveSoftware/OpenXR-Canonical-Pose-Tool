// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#include "action_pose.h"

#include <utility>

#include "util/util_file.h"

PoseInput::PoseInput(PoseActionInfo action_info) : action_info_(std::move(action_info)){};

bool PoseInput::Init(const XrpContext& context, const XrActionSet& action_set, const std::shared_ptr<PoseInput>& base_pose) {
	base_pose_ = base_pose;

	{
		std::vector<XrPath> subaction_paths{};
		for (const std::string& subaction : action_info_.subaction_paths) {
			subaction_paths.emplace_back(XrpStringToXrPath(context, subaction));
		}
		XrActionCreateInfo action_create_info = {
			.type = XR_TYPE_ACTION_CREATE_INFO,
			.next = nullptr,
			.actionType = XR_ACTION_TYPE_POSE_INPUT,
			.countSubactionPaths = (uint32_t)subaction_paths.size(),
			.subactionPaths = subaction_paths.data(),
		};

		action_info_.name.copy(action_create_info.actionName, action_info_.name.size());
		action_info_.name.copy(action_create_info.localizedActionName, action_info_.name.size());

		XRP_CHECK_OR_RETURN(context, xrCreateAction(action_set, &action_create_info, &pose_action_));
	}

	for (const std::string& subaction : action_info_.subaction_paths) {
		XrActionSpaceCreateInfo space_create_info = {
			.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
			.next = nullptr,
			.action = pose_action_,
			.subactionPath = XrpStringToXrPath(context, subaction),
			.poseInActionSpace = xrp_identity_pose,
		};
		XRP_CHECK_OR_RETURN(context, xrCreateActionSpace(context.session, &space_create_info, &action_spaces_[subaction]));
	}

	return true;
}

XrSpace PoseInput::GetActionSpace(const std::string& subaction_path) { return action_spaces_[subaction_path]; }

PoseActionInfo PoseInput::GetActionInfo() { return action_info_; }

bool PoseInput::GetSuggestedBinding(const XrpContext& context, std::vector<XrActionSuggestedBinding>& out_suggested_bindings) {
	for (const std::string& subaction_path : action_info_.subaction_paths) {
		const XrPath binding_path = XrpStringToXrPath(context, subaction_path + action_info_.suggested_binding);

		XrActionSuggestedBinding suggested_binding = {
			.action = pose_action_,
			.binding = binding_path,
		};

		out_suggested_bindings.push_back(suggested_binding);
	}

	return true;
}

bool PoseInput::GetPoseInfo(const XrpContext& context, PoseOutputInfo& out_pose_output_info) {
	if (action_info_.reference) {
		XrpLog("Skipping %s because it was defined as a reference pose", action_info_.name.c_str());
		return true;
	};

	std::vector<PoseInfo> pose_infos;

	for (const std::string& subaction : action_info_.subaction_paths) {
		{
			XrActionStateGetInfo action_state_get_info = {
				.type = XR_TYPE_ACTION_STATE_GET_INFO,
				.next = nullptr,
				.action = pose_action_,
				.subactionPath = XrpStringToXrPath(context, subaction),
			};
			XrActionStatePose pose_state = {
				.type = XR_TYPE_ACTION_STATE_POSE,
				.next = nullptr,
			};
			XRP_CHECK_OR_RETURN(context, xrGetActionStatePose(context.session, &action_state_get_info, &pose_state));

			if (!pose_state.isActive) {
				XrpLog("pose %s is not active.", action_info_.name.c_str());
				return false;
			}
		}

		XrSpace pose_base_space = base_pose_ ? base_pose_->GetActionSpace(subaction) : context.reference_space;
		XrSpaceLocation space_location = {.type = XR_TYPE_SPACE_LOCATION, .next = nullptr};
		XRP_CHECK_OR_RETURN(
			context, xrLocateSpace(action_spaces_[subaction], pose_base_space, context.current_frame_state.predictedDisplayTime, &space_location));

		std::string interaction_profile;
		if (!XrpGetInteractionProfileForUserPath(context, subaction, interaction_profile)) {
			XrpLog("Failed to get interaction profile path");
			return false;
		}

		XrPosef action_pose = space_location.pose;
		StandardizeXrQuaternion(action_pose.orientation);

		if (!(space_location.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT))) {
			XrpLog("A pose component of %s was empty.", action_info_.name.c_str());
			return false;
		}

		PoseInfo info = {
			.action_name = action_info_.name,
			.binding_path = subaction + action_info_.suggested_binding,
			.interaction_profile = interaction_profile,
			.base = base_pose_ ? subaction + base_pose_->GetActionInfo().suggested_binding : "",
			.pose = action_pose,
		};

		pose_infos.emplace_back(info);
	}

	// check if the poses are symmetrical
	out_pose_output_info.check_symmetrical = false;
	out_pose_output_info.is_orientation_symmetrical = false;
	out_pose_output_info.is_position_symmetrical = false;

	if (pose_infos.size() == 2) {
		out_pose_output_info.check_symmetrical = true;

		{
			const XrQuaternionf& q1 = pose_infos[0].pose.orientation;
			const XrQuaternionf& q2 = pose_infos[1].pose.orientation;

			if (XrpCompareFloat(q1.w, q2.w) && XrpCompareFloat(q1.x, q2.x) && XrpCompareFloat(-q1.y, q2.y) && XrpCompareFloat(-q1.z, q2.z)) {
				out_pose_output_info.is_orientation_symmetrical = true;
			}
		}

		{
			const XrVector3f vec1 = pose_infos[0].pose.position;
			const XrVector3f vec2 = pose_infos[1].pose.position;

			if (XrpCompareFloat(vec1.x, -vec2.x) && XrpCompareFloat(vec1.y, vec2.y) && XrpCompareFloat(vec1.z, vec2.z)) {
				out_pose_output_info.is_position_symmetrical = true;
			}
		}
	}

	out_pose_output_info.pose_infos = std::move(pose_infos);

	return true;
}

PoseInput::~PoseInput() {
	if (pose_action_ != XR_NULL_HANDLE) {
		xrDestroyAction(pose_action_);
	}

	for (auto& action_space : action_spaces_) {
		xrDestroySpace(action_space.second);
	}
}