# OpenXR Canonical Pose Tool

Assesses pose positioning from different runtimes to help runtime vendors implement the "canonical" poses that the
vendors of the hardware intend.

## Usage

Start by cloning the repository with submodules:

* `git clone ... --recurse-submodules`

Or, if you already cloned and didn't clone submodules:

* `git submodule update --init`

### PC

Build the project with CMake:

* `mkdir build && cd build`
* `cmake ..`

Open the solution and run the project. The tool will use whatever the runtime is currently active.

### Quest Standalone

Setup:

* Follow: https://developer.oculus.com/documentation/native/android/mobile-device-setup/
* Make sure developer mode is enabled on Quest (have to use mobile app).

Install:

* https://developer.android.com/studio
    * NDK
    * Android SDK
* https://developer.oculus.com/downloads/package/oculus-developer-hub-win

Open the root of the repository in Android Studio. Press Run.

### Output

* PC: the directory where the executable is located.
* Quest: external data path. Looking at the Quest storage on the PC, this
  is `Android/data/com.danwillm.oxr_canonical_pose_tool/files`.

## Configuration

Configuration for the tool is done in `cpt_config.xml`.

Each item will output one file on each run of the tool. Items that will output files on run can be configured
under `outputs`.

### Inputs

Configuration for inputs (items that require the use of an interaction profile) is done under the `inputs` node.

Interaction profiles to suggest binding profiles for are defined in the `interaction_profiles` node. Each child node
should have a name of `interaction_profile` and a value of the interaction profile path. Available attributes of
the `interaction_profile` node are:

* `requires_extension` - Interaction profiles that require an extension to be used can specify the extension to request
  in this attribute.

Actions defined in the `actions` node.

Each child `action` node can have the following attributes:

* `name` - The name of the action.
* `type` - The type of action. Supported values:
    * `pose` - Pose action
* `suggested_binding` - The binding to suggest the action be bound to in the interaction profile. If subaction paths are
  used, these are prefixed to the `suggested_binding`
* `reference` - If `reference` is true, then the action won't be output to the resultant file, but can be used for other
  actions to use as a reference space when retrieving the pose.
* `base` - The name of the action to get the current action's pose space in relation to.
* `requires_extension` - If the action requires an extension to be used, this can be specified using this attribute.

### Runtimes

Runtimes can add their own canonical reference files to `runtimes`, along with a way to match their `runtimeName` in the
OpenXR instance properties to a name to define the runtime by in the tool.

Files output from the tool can be added directly to `cpt_config.xml`, under `runtimes`.

Reference files for inputs should be in `inputs`, with a child node called `input` and must have
an `interaction_profile` attribute specifying the interaction profile that the reference file represents. The value of the node must be the
relative path from the executable to the reference file.