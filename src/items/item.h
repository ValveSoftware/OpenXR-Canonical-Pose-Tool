// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#pragma once

#include <map>
#include <memory>
#include <pugixml.hpp>
#include <string>
#include <vector>

#include "xr/xrp.h"

struct ItemFile {
	pugi::xml_document document;
	std::string name;
};

struct ItemSetOutput {
	std::vector<ItemFile> output_files;
};

class IItemSet {
   public:
	virtual bool GetRequiredExtensions(std::set<std::string>& out_extensions) = 0;
	virtual bool Init(const XrpContext& context) = 0;
	virtual bool GetOutput(const XrpContext& context, ItemSetOutput& out_itemset) = 0;

	virtual ~IItemSet() = default;
};