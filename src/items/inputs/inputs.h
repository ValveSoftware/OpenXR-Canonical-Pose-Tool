// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#pragma once
#include <memory>
#include <vector>

#include "action_pose.h"
#include "items/item.h"
#include "pugixml.hpp"


class InputItemSet : public IItemSet {
   public:
	explicit InputItemSet(pugi::xml_node inputs_config);

	bool GetRequiredExtensions(std::set<std::string>& out_extensions) override;
	bool Init(const XrpContext& context) override;
	bool GetOutput(const XrpContext& context, ItemSetOutput& out_itemset) override;

	~InputItemSet() override;

   private:
	pugi::xml_node config_;

	// <name, pose>
	std::map<std::string, std::shared_ptr<PoseInput>> poses_;
	XrActionSet action_set_{};
};