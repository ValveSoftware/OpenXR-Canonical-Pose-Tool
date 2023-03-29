// Copyright (c) 2023 Valve Corporation
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Daniel Willmott <danw@valvesoftware.com>

#include "xrp.h"

#include <cstdarg>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

#ifdef XR_USE_PLATFORM_ANDROID
static const std::set<std::string> internal_extensions = {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
#else
static const std::set<std::string> internal_extensions = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
#endif

bool XrpSetAvailableExtensions(const XrpApp& app, XrpContext& out_context, uint32_t& available_extension_count,
							  std::vector<std::string>& available_extension_names, uint32_t& app_available_extension_count) {
	uint32_t extension_count = 0;
	XRP_CHECK_OR_RETURN(out_context, xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr));

	std::vector<XrExtensionProperties> extension_properties(extension_count, {.type = XR_TYPE_EXTENSION_PROPERTIES, .next = nullptr});
	XRP_CHECK_OR_RETURN(out_context, xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count, extension_properties.data()));

	std::set<std::string> requested_extensions;
	requested_extensions.insert(app.requested_extensions.begin(), app.requested_extensions.end());
	requested_extensions.insert(internal_extensions.begin(), internal_extensions.end());

	for (const auto& available_extension : extension_properties) {
		std::string current_extension_name = available_extension.extensionName;

		if (!requested_extensions.contains(current_extension_name)) {
			continue;
		}

		out_context.extensions[current_extension_name].available = true;
		available_extension_count++;
		available_extension_names.push_back(current_extension_name);

		// avoid internal required extensions for app.
		if (app.requested_extensions.contains(current_extension_name)) {
			app_available_extension_count++;
			XrpLog("app requested extension: %s is available", available_extension.extensionName);
		} else {
			XrpLog("internally requested extension: %s is available", available_extension.extensionName);
		}
	}

	return true;
}

bool XrpCreateInstance(const XrpApp& app, XrpContext& out_context) {
	uint32_t available_extension_count = 0;
	std::vector<std::string> available_extension_names{};
	uint32_t app_available_extension_count = 0;
	if (!XrpSetAvailableExtensions(app, out_context, available_extension_count, available_extension_names, app_available_extension_count)) {
		XrpLog("failed to get available extensions");
		return false;
	}

	if (app_available_extension_count < app.requested_extensions.size()) {
		XrpLog("not all requested extensions are available!");
	}

	{
		// copy extension names to char array
		std::vector<const char*> extension_names;
		for (auto& extension_name : available_extension_names) {
			extension_names.push_back(extension_name.c_str());
		}

		XrInstanceCreateInfo instance_create_info = {
			.type = XR_TYPE_INSTANCE_CREATE_INFO,
			.createFlags = 0,
			.applicationInfo =
				{
					.applicationVersion = app.app_version,
					.engineVersion = app.engine_version,
					.apiVersion = XR_CURRENT_API_VERSION,
				},
			.enabledApiLayerCount = 0,
			.enabledApiLayerNames = nullptr,
			.enabledExtensionCount = available_extension_count,
			.enabledExtensionNames = extension_names.data(),
		};
		app.app_name.copy(instance_create_info.applicationInfo.applicationName, app.app_name.size());
		app.engine_name.copy(instance_create_info.applicationInfo.engineName, app.engine_name.size());
		XRP_CHECK_OR_RETURN(out_context, xrCreateInstance(&instance_create_info, &out_context.instance));

		XrInstanceProperties instance_properties = {.type = XR_TYPE_INSTANCE_PROPERTIES, .next = nullptr};
		XRP_CHECK_OR_RETURN(out_context, xrGetInstanceProperties(out_context.instance, &instance_properties));
		out_context.instance_properties = instance_properties;

		return true;
	}
}

bool XrpIsExtensionAvailable(const XrpContext& context, const std::string& extension_name) {
	return context.extensions.contains(extension_name) && context.extensions.at(extension_name).available;
}

bool XrpCreateSession(XrpContext& out_context) {
	{
		XrSystemGetInfo system_get_info = {
			.type = XR_TYPE_SYSTEM_GET_INFO,
			.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
		};

		XRP_CHECK_OR_RETURN(out_context, xrGetSystem(out_context.instance, &system_get_info, &out_context.system_id));
	}

	XrSessionCreateInfo session_create_info = {
		.type = XR_TYPE_SESSION_CREATE_INFO,
		.systemId = out_context.system_id,
	};

#ifdef XR_USE_PLATFORM_ANDROID
	if (XrpIsExtensionAvailable(out_context, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) {
		XrGraphicsRequirementsOpenGLESKHR reqOpenGLES{
			.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR,
		};
		PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetOpenGLESGraphicsRequirementsKHR = nullptr;
		XRP_CHECK_OR_RETURN(out_context, xrGetInstanceProcAddr(out_context.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
															   (PFN_xrVoidFunction*)&pfnGetOpenGLESGraphicsRequirementsKHR));

		XRP_CHECK_OR_RETURN(out_context, pfnGetOpenGLESGraphicsRequirementsKHR(out_context.instance, out_context.system_id, &reqOpenGLES));

		EGLDisplay egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

		// clang-format off
		static EGLint const config_attribute_list[] = {
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_BUFFER_SIZE, 32,
			EGL_STENCIL_SIZE, 0,
			EGL_DEPTH_SIZE, 16,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
			EGL_NONE
		};
		// clang-format on

		EGLConfig egl_config;
		EGLint num_config;
		eglChooseConfig(egl_display, nullptr, &egl_config, 1, &num_config);

		EGLContext egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, nullptr);

		XrGraphicsBindingOpenGLESAndroidKHR graphics_binding = {
			.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
			.display = eglGetDisplay(EGL_DEFAULT_DISPLAY),
			.config = egl_config,
			.context = egl_context,
		};

		session_create_info.next = &graphics_binding;
	} else {
		XrpLog("Unsupported graphics extension");

		return false;
	}
#else
	if (XrpIsExtensionAvailable(out_context, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
		XrGraphicsRequirementsOpenGLKHR reqOpenGL{
			.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR,
		};
		PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = nullptr;
		XRP_CHECK_OR_RETURN(out_context, xrGetInstanceProcAddr(out_context.instance, "xrGetOpenGLGraphicsRequirementsKHR",
															   (PFN_xrVoidFunction*)&pfnGetOpenGLGraphicsRequirementsKHR));

		XRP_CHECK_OR_RETURN(out_context, pfnGetOpenGLGraphicsRequirementsKHR(out_context.instance, out_context.system_id, &reqOpenGL));

#ifdef XR_USE_PLATFORM_WIN32
		XrGraphicsBindingOpenGLWin32KHR graphics_binding = {
			.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR,
			.hDC = wglGetCurrentDC(),
			.hGLRC = wglGetCurrentContext(),
		};

#endif
#ifdef XR_USE_PLATFORM_XLIB
		XrGraphicsBindingOpenGLXlibKHR graphics_binding = {
			.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
			.next = nullptr,
			.xDisplay = XOpenDisplay(nullptr),
			.glxFBConfig = nullptr,
			.glxDrawable = glXGetCurrentDrawable(),
			.glxContext = glXGetCurrentContext(),
		};
#endif
		session_create_info.next = &graphics_binding;
	}

#ifdef XR_USE_PLATFORM_WIN32
	if (XrpIsExtensionAvailable(out_context, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) && !session_create_info.next) {
		PFN_xrGetD3D12GraphicsRequirementsKHR pfnGetD3D12GraphicsRequirementsKHR = nullptr;
		XRP_CHECK_OR_RETURN(out_context, xrGetInstanceProcAddr(out_context.instance, "xrGetD3D12GraphicsRequirementsKHR",
															   reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetD3D12GraphicsRequirementsKHR)));

		XrGraphicsRequirementsD3D12KHR graphics_requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
		XRP_CHECK_OR_RETURN(out_context, pfnGetD3D12GraphicsRequirementsKHR(out_context.instance, out_context.system_id, &graphics_requirements));

		ID3D12Device* d3d_device = nullptr;
		{
			IDXGIFactory* factory = nullptr;
			if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&factory)))) {
				XrpLog("Failed to create DXGIFactory");

				return false;
			}

			IDXGIAdapter* adapter = nullptr;
			for (UINT adapterIndex = 0;; ++adapterIndex) {
				if (DXGI_ERROR_NOT_FOUND == factory->EnumAdapters(adapterIndex, &adapter)) {
					// No more adapters to enumerate.
					break;
				}

				DXGI_ADAPTER_DESC desc;
				if (SUCCEEDED(adapter->GetDesc(&desc))) {
					if (memcmp(&desc.AdapterLuid, &graphics_requirements.adapterLuid, sizeof(graphics_requirements.adapterLuid)) == 0) {
						break;
					}
				} else {
					XrpLog("Adapter GetDesc failed");
				}

				adapter->Release();
			}

			if (FAILED(D3D12CreateDevice(adapter, graphics_requirements.minFeatureLevel, IID_PPV_ARGS(&d3d_device)))) {
				XrpLog("D3D12CreateDevice failed");

				return false;
			}
		}

		if (!d3d_device) {
			XrpLog("D3D device not created successfully");

			return false;
		}

		ID3D12CommandQueue* queue = nullptr;
		D3D12_COMMAND_QUEUE_DESC queue_desc = {};
		queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (FAILED(d3d_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) {
			XrpLog("Failed to create command queue");

			return false;
		}

		XrGraphicsBindingD3D12KHR graphics_binding = {.type = XR_TYPE_GRAPHICS_BINDING_D3D12_KHR, .device = d3d_device, .queue = queue};
		session_create_info.next = &graphics_binding;
	}
#endif

#endif

	XRP_CHECK_OR_RETURN(out_context, xrCreateSession(out_context.instance, &session_create_info, &out_context.session));

	{
		XrReferenceSpaceCreateInfo reference_space_create_info = {
			.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
			.next = nullptr,
			.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
			.poseInReferenceSpace = xrp_identity_pose,
		};
		XRP_CHECK_OR_RETURN(out_context, xrCreateReferenceSpace(out_context.session, &reference_space_create_info, &out_context.reference_space));
	}

	return true;
}

XrPath XrpStringToXrPath(const XrpContext& context, const std::string& path) {
	XrPath xr_path;
	XRP_CHECK_OR_RETURN(context, xrStringToPath(context.instance, path.c_str(), &xr_path));

	return xr_path;
}

bool XrpXrPathToString(const XrpContext& context, const XrPath path, std::string& out_path) {
	char buffer[XR_MAX_PATH_LENGTH];
	uint32_t written;
	if (xrPathToString(context.instance, path, sizeof(buffer), &written, buffer) != XR_SUCCESS) {
		XrpLog("failed to get string for path");

		return false;
	};

	out_path = buffer;

	return true;
}

bool XrpGetInteractionProfileForUserPath(const XrpContext& context, const std::string& user_path, std::string& out_interaction_profile) {
	const XrPath subaction_path = XrpStringToXrPath(context, user_path);
	XrInteractionProfileState interaction_profile_state = {.type = XR_TYPE_INTERACTION_PROFILE_STATE};
	XRP_CHECK_OR_RETURN(context, xrGetCurrentInteractionProfile(context.session, subaction_path, &interaction_profile_state));

	return XrpXrPathToString(context, interaction_profile_state.interactionProfile, out_interaction_profile);
}

#ifdef XR_USE_PLATFORM_ANDROID
extern android_app* gapp;
#endif

bool XrpInit(const XrpApp& app, XrpContext& out_context) {
#ifdef XR_USE_PLATFORM_ANDROID
	PFN_xrInitializeLoaderKHR pfnInitializeLoader;
	XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&pfnInitializeLoader);
	if (result != XR_SUCCESS) {
		XrpLog("unable to initialize android loader: %i", result);
		return false;
	}

	XrLoaderInitInfoAndroidKHR init_data = {
		.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
	};
	struct android_app* aapp = gapp;
	const JavaVM* jniiptr = aapp->activity->vm;
	jobject activity = aapp->activity->clazz;
	init_data.applicationVM = (void*)jniiptr;
	init_data.applicationContext = activity;
	pfnInitializeLoader((XrLoaderInitInfoBaseHeaderKHR*)&init_data);

	XrpLog("android openxr loader initialized");
#endif

	if (!XrpCreateInstance(app, out_context)) {
		XrpLog("failed to create xr instance");

		return false;
	}

	if (!XrpCreateSession(out_context)) {
		XrpLog("failed to create xr session");
		return false;
	}

	XrpLog("openxr initialized successfully");

	return true;
}

bool XrpEndSession(const XrpContext& context) {
	XrpLog("ending xr session");
	XRP_CHECK_OR_RETURN(context, xrEndSession(context.session));
	return true;
}

static bool exit_requested = false;

bool XrpRunFrameLoop(XrpContext& context, const std::function<bool(XrpEvent, const XrpEventData&)>& event_callback) {
	if (context.session == XR_NULL_HANDLE) {
		XrpLog("session is invalid");
		return false;
	}
	bool should_exit = false;
	bool session_running = false;
	bool run_framecycle = false;

	XrSessionState current_session_state;

	// Main loop
	while (!should_exit) {
		XrEventDataBuffer runtime_event = {.type = XR_TYPE_EVENT_DATA_BUFFER};

		XrResult result = xrPollEvent(context.instance, &runtime_event);
		while (result == XR_SUCCESS) {
			switch (runtime_event.type) {
				case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
					XrpLog("runtime state updated: %s", "XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING");
					should_exit = true;
					break;
				}
				case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
					auto* session_state_changed_event = (XrEventDataSessionStateChanged*)&runtime_event;
					XrpLog("XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %i", session_state_changed_event->state);

					current_session_state = session_state_changed_event->state;

					switch (session_state_changed_event->state) {
						case XR_SESSION_STATE_IDLE:
						case XR_SESSION_STATE_UNKNOWN: {
							XrpLog("not running frame cycle as session is not ready");
							run_framecycle = false;
							break;
						}
						case XR_SESSION_STATE_READY: {
							if (session_running) {
								XrpLog("session already running!");
								break;
							}

							XrSessionBeginInfo session_begin_info = {
								.type = XR_TYPE_SESSION_BEGIN_INFO,
								.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
							};
							XRP_CHECK_OR_RETURN(context, xrBeginSession(context.session, &session_begin_info));

							XrpLog("session has begun");

							session_running = true;
							run_framecycle = true;

							event_callback(XRP_EVENT_SESSION_READY, {.session_state = current_session_state});
							break;
						};

						case XR_SESSION_STATE_FOCUSED: {
							event_callback(XRP_EVENT_SESSION_FOCUSED, {.session_state = current_session_state});
							break;
						}

						case XR_SESSION_STATE_STOPPING: {
							run_framecycle = false;
							XrpEndSession(context);

							break;
						}

						case XR_SESSION_STATE_LOSS_PENDING:
						case XR_SESSION_STATE_EXITING: {
							XRP_CHECK_OR_RETURN(context, xrDestroySession(context.session));

							should_exit = true;
							run_framecycle = false;
							break;
						}

						default:
							break;
					}

					break;
				}
				default: {
					break;
				}
			}

			runtime_event.type = XR_TYPE_EVENT_DATA_BUFFER;
			result = xrPollEvent(context.instance, &runtime_event);
		}
		if (!run_framecycle || exit_requested) continue;

		XrFrameState frame_state = {
			.type = XR_TYPE_FRAME_STATE,
			.next = nullptr,
			.shouldRender = true,
		};
		XrFrameWaitInfo frame_wait_info = {.type = XR_TYPE_FRAME_WAIT_INFO, .next = nullptr};
		XRP_CHECK_OR_RETURN(context, xrWaitFrame(context.session, &frame_wait_info, &frame_state));
		context.current_frame_state = frame_state;

		XrFrameBeginInfo frame_begin_info = {.type = XR_TYPE_FRAME_BEGIN_INFO, .next = nullptr};
		XRP_CHECK_OR_RETURN(context, xrBeginFrame(context.session, &frame_begin_info));

		event_callback(XRP_EVENT_DO_FRAME, {.session_state = current_session_state});

		XrFrameEndInfo frame_end_info = {
			.type = XR_TYPE_FRAME_END_INFO,
			.next = nullptr,
			.displayTime = frame_state.predictedDisplayTime,
			.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		};
		XRP_CHECK_OR_RETURN(context, xrEndFrame(context.session, &frame_end_info));

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return true;
}

bool XrpRequestExitSession(const XrpContext& context) {
	XrpLog("requesting xr session exit");
	exit_requested = true;
	XRP_CHECK_OR_RETURN(context, xrRequestExitSession(context.session));

	return true;
}

bool XrpDestroy(XrpContext& context) {
	XRP_CHECK_OR_RETURN(context, xrDestroyInstance(context.instance));

	return true;
}

std::string XrpRoundFloatToString(float value, int precision) {
	std::stringstream stream;
	stream << std::fixed << std::setprecision(precision) << value;

	return stream.str();
}

#if !defined(WIN32)
#define vsnprintf_s vsnprintf
#endif
void XrpLog(const char* format, ...) {
	va_list args;
	va_start(args, format);

#ifdef XR_USE_PLATFORM_ANDROID
	__android_log_vprint(ANDROID_LOG_INFO, "danwillm", format, args);
#else
	char buf[1024];
	vsnprintf_s(buf, sizeof(buf), format, args);

	std::cout << buf << std::endl;
#endif

	va_end(args);
}