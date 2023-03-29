// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#pragma once

#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>

#define _USE_MATH_DEFINES
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// android includes
#ifdef XR_USE_PLATFORM_ANDROID
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <sys/system_properties.h>
#endif

// windows includes
#ifdef XR_USE_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>
#include <dxgi.h>
#include <unknwn.h>
#include <windows.h>
#endif

// linux includes
#ifdef XR_USE_PLATFORM_XLIB
#include <GL/glx.h>
#include <X11/Xlib.h>
#endif

#ifdef XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif

#ifdef XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_D3D12
#endif

#ifdef XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#endif

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

#define XRP_CHECK_OR_RETURN(context, func)                                                                                    \
	do {                                                                                                                      \
		XrResult xrpresult = func;                                                                                            \
		if (!XR_UNQUALIFIED_SUCCESS(xrpresult)) {                                                                             \
			char xrperr[XR_MAX_RESULT_STRING_SIZE];                                                                           \
			xrResultToString(context.instance, xrpresult, xrperr);                                                            \
			std::cout << __FILE__ << ": " << __LINE__ << " - Failed to call " << #func << ". Error: " << xrperr << std::endl; \
			return false;                                                                                                     \
		}                                                                                                                     \
	} while (false)

static const XrPosef xrp_identity_pose = {
	.orientation = {0, 0, 0, 1.0},
	.position = {0, 0, 0},
};

enum XrpEvent {
	XRP_EVENT_SESSION_READY,
	XRP_EVENT_SESSION_FOCUSED,
	XRP_EVENT_DO_FRAME,
};

struct XrpEventData {
	XrSessionState session_state;
};

struct XrpApp {
	std::string app_name;
	uint32_t app_version;

	std::string engine_name;
	uint32_t engine_version;

	std::set<std::string> requested_extensions;
};

struct XrpExtension {
	bool available = false;
};

struct XrpContext {
	XrInstance instance;
	XrSession session;
	XrSystemId system_id;

	XrInstanceProperties instance_properties;
	XrSpace reference_space;
	XrFrameState current_frame_state;

	std::map<std::string, XrpExtension> extensions;
};

struct XrpEulerAngles {
	float x;
	float y;
	float z;
};

XrPath XrpStringToXrPath(const XrpContext& context, const std::string& path);
bool XrpXrPathToString(const XrpContext& context, XrPath path, std::string& out_path);

bool XrpGetInteractionProfileForUserPath(const XrpContext& context, const std::string& user_path, std::string& out_interaction_profile);

bool XrpIsExtensionAvailable(const XrpContext& context, const std::string& extension_name);

bool XrpInit(const XrpApp& app, XrpContext& out_context);
bool XrpRunFrameLoop(XrpContext& context, const std::function<bool(XrpEvent, const XrpEventData&)>& event_callback);

bool XrpRequestExitSession(const XrpContext& context);

bool XrpDestroy(XrpContext& context);

void XrpLog(const char* format, ...);

static bool XrpCompareFloat(float x, float y, float tolerance = 0.01f) {
	if (fabs(x - y) < tolerance) {
		return true;
	}

	return false;
}

static XrQuaternionf operator*(const XrQuaternionf& lhs, const XrQuaternionf& rhs) {
	return {
		.x = lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
		.y = lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
		.z = lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
		.w = lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
	};
}

static XrQuaternionf operator-(const XrQuaternionf& q) {
	return {
		.x = -q.x,
		.y = -q.y,
		.z = -q.z,
		.w = q.w,
	};
}

static void StandardizeXrQuaternion(XrQuaternionf& q) {
	if (q.w >= 0.f) {
		return;
	}

	q.w = -q.w;
	q.x = -q.x;
	q.y = -q.y;
	q.z = -q.z;
}

static bool operator!(const XrQuaternionf& q) { return q.w == 0.f && q.x == 0.f && q.y == 0.f && q.z == 0.f; }

static bool operator==(const XrQuaternionf& q1, const XrQuaternionf& q2) {
	return (XrpCompareFloat(q1.w, q2.w) && XrpCompareFloat(q1.x, q2.x) && XrpCompareFloat(q1.y, q2.y) && XrpCompareFloat(q1.z, q2.z));
}

static XrVector3f operator*(const XrVector3f& vec, const XrQuaternionf& q) {
	const XrQuaternionf qvec = {.x = vec.x, .y = vec.y, .z = vec.z, .w = 0.f};

	const XrQuaternionf qResult = (q * qvec) * (-q);

	return {
		.x = qResult.x,
		.y = qResult.y,
		.z = qResult.z,
	};
}

static XrVector3f operator-(const XrVector3f& vec1, const XrVector3f& vec2) {
	return {
		.x = vec1.x - vec2.x,
		.y = vec1.y - vec2.y,
		.z = vec1.z - vec2.z,
	};
}

static bool operator==(const XrVector3f& vec1, const XrVector3f& vec2) {
	return XrpCompareFloat(vec1.x, vec2.x) && XrpCompareFloat(vec1.y, vec2.y) && XrpCompareFloat(vec1.z, vec2.z);
}

std::string XrpRoundFloatToString(float value, int precision);