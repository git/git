/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2021 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/**
 *  \file SDL_hints.h
 *
 *  Official documentation for SDL configuration variables
 *
 *  This file contains functions to set and get configuration hints,
 *  as well as listing each of them alphabetically.
 *
 *  The convention for naming hints is SDL_HINT_X, where "SDL_X" is
 *  the environment variable that can be used to override the default.
 *
 *  In general these hints are just that - they may or may not be
 *  supported or applicable on any given platform, but they provide
 *  a way for an application or user to give the library a hint as
 *  to how they would like the library to work.
 */

#ifndef SDL_hints_h_
#define SDL_hints_h_

#include "SDL_stdinc.h"

#include "begin_code.h"
/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 *  \brief  A variable controlling whether the Android / iOS built-in
 *  accelerometer should be listed as a joystick device.
 *
 *  This variable can be set to the following values:
 *    "0"       - The accelerometer is not listed as a joystick
 *    "1"       - The accelerometer is available as a 3 axis joystick (the default).
 */
#define SDL_HINT_ACCELEROMETER_AS_JOYSTICK "SDL_ACCELEROMETER_AS_JOYSTICK"

/**
 *  \brief Specify the behavior of Alt+Tab while the keyboard is grabbed.
 *
 * By default, SDL emulates Alt+Tab functionality while the keyboard is grabbed
 * and your window is full-screen. This prevents the user from getting stuck in
 * your application if you've enabled keyboard grab.
 *
 * The variable can be set to the following values:
 *   "0"       - SDL will not handle Alt+Tab. Your application is responsible
                 for handling Alt+Tab while the keyboard is grabbed.
 *   "1"       - SDL will minimize your window when Alt+Tab is pressed (default)
*/
#define SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED "SDL_ALLOW_ALT_TAB_WHILE_GRABBED"

/**
 *  \brief If set to "0" then never set the top most bit on a SDL Window, even if the video mode expects it.
 *      This is a debugging aid for developers and not expected to be used by end users. The default is "1"
 *
 *  This variable can be set to the following values:
 *    "0"       - don't allow topmost
 *    "1"       - allow topmost
 */
#define SDL_HINT_ALLOW_TOPMOST "SDL_ALLOW_TOPMOST"

/**
 * \brief Android APK expansion main file version. Should be a string number like "1", "2" etc.
 *
 * Must be set together with SDL_HINT_ANDROID_APK_EXPANSION_PATCH_FILE_VERSION.
 *
 * If both hints were set then SDL_RWFromFile() will look into expansion files
 * after a given relative path was not found in the internal storage and assets.
 *
 * By default this hint is not set and the APK expansion files are not searched.
 */
#define SDL_HINT_ANDROID_APK_EXPANSION_MAIN_FILE_VERSION "SDL_ANDROID_APK_EXPANSION_MAIN_FILE_VERSION"
 
/**
 * \brief Android APK expansion patch file version. Should be a string number like "1", "2" etc.
 *
 * Must be set together with SDL_HINT_ANDROID_APK_EXPANSION_MAIN_FILE_VERSION.
 *
 * If both hints were set then SDL_RWFromFile() will look into expansion files
 * after a given relative path was not found in the internal storage and assets.
 *
 * By default this hint is not set and the APK expansion files are not searched.
 */
#define SDL_HINT_ANDROID_APK_EXPANSION_PATCH_FILE_VERSION "SDL_ANDROID_APK_EXPANSION_PATCH_FILE_VERSION"

/**
 * \brief A variable to control whether the event loop will block itself when the app is paused.
 *
 * The variable can be set to the following values:
 *   "0"       - Non blocking.
 *   "1"       - Blocking. (default)
 *
 * The value should be set before SDL is initialized.
 */
#define SDL_HINT_ANDROID_BLOCK_ON_PAUSE "SDL_ANDROID_BLOCK_ON_PAUSE"

/**
 * \brief A variable to control whether SDL will pause audio in background
 *        (Requires SDL_ANDROID_BLOCK_ON_PAUSE as "Non blocking")
 *
 * The variable can be set to the following values:
 *   "0"       - Non paused.
 *   "1"       - Paused. (default)
 *
 * The value should be set before SDL is initialized.
 */
#define SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO "SDL_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO"

/**
 * \brief A variable to control whether we trap the Android back button to handle it manually.
 *        This is necessary for the right mouse button to work on some Android devices, or
 *        to be able to trap the back button for use in your code reliably.  If set to true,
 *        the back button will show up as an SDL_KEYDOWN / SDL_KEYUP pair with a keycode of 
 *        SDL_SCANCODE_AC_BACK.
 *
 * The variable can be set to the following values:
 *   "0"       - Back button will be handled as usual for system. (default)
 *   "1"       - Back button will be trapped, allowing you to handle the key press
 *               manually.  (This will also let right mouse click work on systems 
 *               where the right mouse button functions as back.)
 *
 * The value of this hint is used at runtime, so it can be changed at any time.
 */
#define SDL_HINT_ANDROID_TRAP_BACK_BUTTON "SDL_ANDROID_TRAP_BACK_BUTTON"

/**
 *  \brief Specify an application name.
 * 
 * This hint lets you specify the application name sent to the OS when
 * required. For example, this will often appear in volume control applets for
 * audio streams, and in lists of applications which are inhibiting the
 * screensaver.  You should use a string that describes your program ("My Game
 * 2: The Revenge")
 *
 * Setting this to "" or leaving it unset will have SDL use a reasonable
 * default: probably the application's name or "SDL Application" if SDL
 * doesn't have any better information.
 *
 * Note that, for audio streams, this can be overridden with
 * SDL_HINT_AUDIO_DEVICE_APP_NAME.
 *
 * On targets where this is not supported, this hint does nothing.
 */
#define SDL_HINT_APP_NAME "SDL_APP_NAME"

/**
 *  \brief  A variable controlling whether controllers used with the Apple TV
 *  generate UI events.
 *
 * When UI events are generated by controller input, the app will be
 * backgrounded when the Apple TV remote's menu button is pressed, and when the
 * pause or B buttons on gamepads are pressed.
 *
 * More information about properly making use of controllers for the Apple TV
 * can be found here:
 * https://developer.apple.com/tvos/human-interface-guidelines/remote-and-controllers/
 *
 *  This variable can be set to the following values:
 *    "0"       - Controller input does not generate UI events (the default).
 *    "1"       - Controller input generates UI events.
 */
#define SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS "SDL_APPLE_TV_CONTROLLER_UI_EVENTS"

/**
 * \brief  A variable controlling whether the Apple TV remote's joystick axes
 *         will automatically match the rotation of the remote.
 *
 *  This variable can be set to the following values:
 *    "0"       - Remote orientation does not affect joystick axes (the default).
 *    "1"       - Joystick axes are based on the orientation of the remote.
 */
#define SDL_HINT_APPLE_TV_REMOTE_ALLOW_ROTATION "SDL_APPLE_TV_REMOTE_ALLOW_ROTATION"

/**
 *  \brief  A variable controlling the audio category on iOS and Mac OS X
 *
 *  This variable can be set to the following values:
 *
 *    "ambient"     - Use the AVAudioSessionCategoryAmbient audio category, will be muted by the phone mute switch (default)
 *    "playback"    - Use the AVAudioSessionCategoryPlayback category
 *
 *  For more information, see Apple's documentation:
 *  https://developer.apple.com/library/content/documentation/Audio/Conceptual/AudioSessionProgrammingGuide/AudioSessionCategoriesandModes/AudioSessionCategoriesandModes.html
 */
#define SDL_HINT_AUDIO_CATEGORY   "SDL_AUDIO_CATEGORY"

/**
 *  \brief Specify an application name for an audio device.
 *
 * Some audio backends (such as PulseAudio) allow you to describe your audio
 * stream. Among other things, this description might show up in a system
 * control panel that lets the user adjust the volume on specific audio
 * streams instead of using one giant master volume slider.
 *
 * This hints lets you transmit that information to the OS. The contents of
 * this hint are used while opening an audio device. You should use a string
 * that describes your program ("My Game 2: The Revenge")
 *
 * Setting this to "" or leaving it unset will have SDL use a reasonable
 * default: this will be the name set with SDL_HINT_APP_NAME, if that hint is
 * set. Otherwise, it'll probably the application's name or "SDL Application"
 * if SDL doesn't have any better information.
 *
 * On targets where this is not supported, this hint does nothing.
 */
#define SDL_HINT_AUDIO_DEVICE_APP_NAME "SDL_AUDIO_DEVICE_APP_NAME"

/**
 *  \brief Specify an application name for an audio device.
 *
 * Some audio backends (such as PulseAudio) allow you to describe your audio
 * stream. Among other things, this description might show up in a system
 * control panel that lets the user adjust the volume on specific audio
 * streams instead of using one giant master volume slider.
 *
 * This hints lets you transmit that information to the OS. The contents of
 * this hint are used while opening an audio device. You should use a string
 * that describes your what your program is playing ("audio stream" is
 * probably sufficient in many cases, but this could be useful for something
 * like "team chat" if you have a headset playing VoIP audio separately).
 *
 * Setting this to "" or leaving it unset will have SDL use a reasonable
 * default: "audio stream" or something similar.
 *
 * On targets where this is not supported, this hint does nothing.
 */
#define SDL_HINT_AUDIO_DEVICE_STREAM_NAME "SDL_AUDIO_DEVICE_STREAM_NAME"

/**
 *  \brief Specify an application role for an audio device.
 *
 * Some audio backends (such as Pipewire) allow you to describe the role of
 * your audio stream. Among other things, this description might show up in
 * a system control panel or software for displaying and manipulating media
 * playback/capture graphs.
 *
 * This hints lets you transmit that information to the OS. The contents of
 * this hint are used while opening an audio device. You should use a string
 * that describes your what your program is playing (Game, Music, Movie,
 * etc...).
 *
 * Setting this to "" or leaving it unset will have SDL use a reasonable
 * default: "Game" or something similar.
 *
 * On targets where this is not supported, this hint does nothing.
 */
#define SDL_HINT_AUDIO_DEVICE_STREAM_ROLE "SDL_AUDIO_DEVICE_STREAM_ROLE"

/**
 *  \brief  A variable controlling speed/quality tradeoff of audio resampling.
 *
 *  If available, SDL can use libsamplerate ( http://www.mega-nerd.com/SRC/ )
 *  to handle audio resampling. There are different resampling modes available
 *  that produce different levels of quality, using more CPU.
 *
 *  If this hint isn't specified to a valid setting, or libsamplerate isn't
 *  available, SDL will use the default, internal resampling algorithm.
 *
 *  Note that this is currently only applicable to resampling audio that is
 *  being written to a device for playback or audio being read from a device
 *  for capture. SDL_AudioCVT always uses the default resampler (although this
 *  might change for SDL 2.1).
 *
 *  This hint is currently only checked at audio subsystem initialization.
 *
 *  This variable can be set to the following values:
 *
 *    "0" or "default" - Use SDL's internal resampling (Default when not set - low quality, fast)
 *    "1" or "fast"    - Use fast, slightly higher quality resampling, if available
 *    "2" or "medium"  - Use medium quality resampling, if available
 *    "3" or "best"    - Use high quality resampling, if available
 */
#define SDL_HINT_AUDIO_RESAMPLING_MODE   "SDL_AUDIO_RESAMPLING_MODE"

/**
 *  \brief  A variable controlling whether SDL updates joystick state when getting input events
 *
 *  This variable can be set to the following values:
 *
 *    "0"     - You'll call SDL_JoystickUpdate() manually
 *    "1"     - SDL will automatically call SDL_JoystickUpdate() (default)
 *
 *  This hint can be toggled on and off at runtime.
 */
#define SDL_HINT_AUTO_UPDATE_JOYSTICKS  "SDL_AUTO_UPDATE_JOYSTICKS"

/**
 *  \brief  A variable controlling whether SDL updates sensor state when getting input events
 *
 *  This variable can be set to the following values:
 *
 *    "0"     - You'll call SDL_SensorUpdate() manually
 *    "1"     - SDL will automatically call SDL_SensorUpdate() (default)
 *
 *  This hint can be toggled on and off at runtime.
 */
#define SDL_HINT_AUTO_UPDATE_SENSORS    "SDL_AUTO_UPDATE_SENSORS"

/**
 *  \brief Prevent SDL from using version 4 of the bitmap header when saving BMPs.
 *
 * The bitmap header version 4 is required for proper alpha channel support and
 * SDL will use it when required. Should this not be desired, this hint can
 * force the use of the 40 byte header version which is supported everywhere.
 *
 * The variable can be set to the following values:
 *   "0"       - Surfaces with a colorkey or an alpha channel are saved to a
 *               32-bit BMP file with an alpha mask. SDL will use the bitmap
 *               header version 4 and set the alpha mask accordingly.
 *   "1"       - Surfaces with a colorkey or an alpha channel are saved to a
 *               32-bit BMP file without an alpha mask. The alpha channel data
 *               will be in the file, but applications are going to ignore it.
 *
 * The default value is "0".
 */
#define SDL_HINT_BMP_SAVE_LEGACY_FORMAT "SDL_BMP_SAVE_LEGACY_FORMAT"

/**
 *  \brief Override for SDL_GetDisplayUsableBounds()
 *
 *  If set, this hint will override the expected results for
 *  SDL_GetDisplayUsableBounds() for display index 0. Generally you don't want
 *  to do this, but this allows an embedded system to request that some of the
 *  screen be reserved for other uses when paired with a well-behaved
 *  application.
 *
 *  The contents of this hint must be 4 comma-separated integers, the first
 *  is the bounds x, then y, width and height, in that order.
 */
#define SDL_HINT_DISPLAY_USABLE_BOUNDS "SDL_DISPLAY_USABLE_BOUNDS"

/**
 *  \brief Disable giving back control to the browser automatically
 *  when running with asyncify
 *
 * With -s ASYNCIFY, SDL2 calls emscripten_sleep during operations
 * such as refreshing the screen or polling events.
 *
 * This hint only applies to the emscripten platform
 *
 * The variable can be set to the following values:
 *    "0"       - Disable emscripten_sleep calls (if you give back browser control manually or use asyncify for other purposes)
 *    "1"       - Enable emscripten_sleep calls (the default)
 */
#define SDL_HINT_EMSCRIPTEN_ASYNCIFY   "SDL_EMSCRIPTEN_ASYNCIFY"

/**
 *  \brief override the binding element for keyboard inputs for Emscripten builds
 *
 * This hint only applies to the emscripten platform
 *
 * The variable can be one of
 *    "#window"      - The javascript window object (this is the default)
 *    "#document"    - The javascript document object
 *    "#screen"      - the javascript window.screen object
 *    "#canvas"      - the WebGL canvas element
 *    any other string without a leading # sign applies to the element on the page with that ID.
 */
#define SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT   "SDL_EMSCRIPTEN_KEYBOARD_ELEMENT"

/**
 *  \brief  A variable that controls whether Steam Controllers should be exposed using the SDL joystick and game controller APIs
 *
 *  The variable can be set to the following values:
 *    "0"       - Do not scan for Steam Controllers
 *    "1"       - Scan for Steam Controllers (the default)
 *
 *  The default value is "1".  This hint must be set before initializing the joystick subsystem.
 */
#define SDL_HINT_ENABLE_STEAM_CONTROLLERS "SDL_ENABLE_STEAM_CONTROLLERS"

/**
 *  \brief  A variable controlling whether SDL logs all events pushed onto its internal queue.
 *
 *  This variable can be set to the following values:
 *
 *    "0"     - Don't log any events (default)
 *    "1"     - Log all events except mouse and finger motion, which are pretty spammy.
 *    "2"     - Log all events.
 *
 *  This is generally meant to be used to debug SDL itself, but can be useful
 *  for application developers that need better visibility into what is going
 *  on in the event queue. Logged events are sent through SDL_Log(), which
 *  means by default they appear on stdout on most platforms or maybe
 *  OutputDebugString() on Windows, and can be funneled by the app with
 *  SDL_LogSetOutputFunction(), etc.
 *
 *  This hint can be toggled on and off at runtime, if you only need to log
 *  events for a small subset of program execution.
 */
#define SDL_HINT_EVENT_LOGGING   "SDL_EVENT_LOGGING"

/**
 *  \brief  A variable controlling how 3D acceleration is used to accelerate the SDL screen surface.
 *
 *  SDL can try to accelerate the SDL screen surface by using streaming
 *  textures with a 3D rendering engine.  This variable controls whether and
 *  how this is done.
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable 3D acceleration
 *    "1"       - Enable 3D acceleration, using the default renderer.
 *    "X"       - Enable 3D acceleration, using X where X is one of the valid rendering drivers.  (e.g. "direct3d", "opengl", etc.)
 *
 *  By default SDL tries to make a best guess for each platform whether
 *  to use acceleration or not.
 */
#define SDL_HINT_FRAMEBUFFER_ACCELERATION   "SDL_FRAMEBUFFER_ACCELERATION"

/**
 *  \brief  A variable that lets you manually hint extra gamecontroller db entries.
 *
 *  The variable should be newline delimited rows of gamecontroller config data, see SDL_gamecontroller.h
 *
 *  This hint must be set before calling SDL_Init(SDL_INIT_GAMECONTROLLER)
 *  You can update mappings after the system is initialized with SDL_GameControllerMappingForGUID() and SDL_GameControllerAddMapping()
 */
#define SDL_HINT_GAMECONTROLLERCONFIG "SDL_GAMECONTROLLERCONFIG"

/**
 *  \brief  A variable that lets you provide a file with extra gamecontroller db entries.
 *
 *  The file should contain lines of gamecontroller config data, see SDL_gamecontroller.h
 *
 *  This hint must be set before calling SDL_Init(SDL_INIT_GAMECONTROLLER)
 *  You can update mappings after the system is initialized with SDL_GameControllerMappingForGUID() and SDL_GameControllerAddMapping()
 */
#define SDL_HINT_GAMECONTROLLERCONFIG_FILE "SDL_GAMECONTROLLERCONFIG_FILE"

/**
 *  \brief  A variable that overrides the automatic controller type detection
 *
 *  The variable should be comma separated entries, in the form: VID/PID=type
 *
 *  The VID and PID should be hexadecimal with exactly 4 digits, e.g. 0x00fd
 *
 *  The type should be one of:
 *      Xbox360
 *      XboxOne
 *      PS3
 *      PS4
 *      PS5
 *      SwitchPro
 *
 *  This hint affects what driver is used, and must be set before calling SDL_Init(SDL_INIT_GAMECONTROLLER)
 */
#define SDL_HINT_GAMECONTROLLERTYPE "SDL_GAMECONTROLLERTYPE"

/**
 *  \brief  A variable containing a list of devices to skip when scanning for game controllers.
 *
 *  The format of the string is a comma separated list of USB VID/PID pairs
 *  in hexadecimal form, e.g.
 *
 *      0xAAAA/0xBBBB,0xCCCC/0xDDDD
 *
 *  The variable can also take the form of @file, in which case the named
 *  file will be loaded and interpreted as the value of the variable.
 */
#define SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES "SDL_GAMECONTROLLER_IGNORE_DEVICES"

/**
 *  \brief  If set, all devices will be skipped when scanning for game controllers except for the ones listed in this variable.
 *
 *  The format of the string is a comma separated list of USB VID/PID pairs
 *  in hexadecimal form, e.g.
 *
 *      0xAAAA/0xBBBB,0xCCCC/0xDDDD
 *
 *  The variable can also take the form of @file, in which case the named
 *  file will be loaded and interpreted as the value of the variable.
 */
#define SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"

/**
 *  \brief  If set, game controller face buttons report their values according to their labels instead of their positional layout.
 * 
 *  For example, on Nintendo Switch controllers, normally you'd get:
 *
 *      (Y)
 *  (X)     (B)
 *      (A)
 *
 *  but if this hint is set, you'll get:
 *
 *      (X)
 *  (Y)     (A)
 *      (B)
 *
 *  The variable can be set to the following values:
 *    "0"       - Report the face buttons by position, as though they were on an Xbox controller.
 *    "1"       - Report the face buttons by label instead of position
 *
 *  The default value is "1".  This hint may be set at any time.
 */
#define SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS "SDL_GAMECONTROLLER_USE_BUTTON_LABELS"

/**
 *  \brief  A variable controlling whether grabbing input grabs the keyboard
 *
 *  This variable can be set to the following values:
 *    "0"       - Grab will affect only the mouse
 *    "1"       - Grab will affect mouse and keyboard
 *
 *  By default SDL will not grab the keyboard so system shortcuts still work.
 */
#define SDL_HINT_GRAB_KEYBOARD              "SDL_GRAB_KEYBOARD"

/**
 *  \brief  A variable controlling whether the idle timer is disabled on iOS.
 *
 *  When an iOS app does not receive touches for some time, the screen is
 *  dimmed automatically. For games where the accelerometer is the only input
 *  this is problematic. This functionality can be disabled by setting this
 *  hint.
 *
 *  As of SDL 2.0.4, SDL_EnableScreenSaver() and SDL_DisableScreenSaver()
 *  accomplish the same thing on iOS. They should be preferred over this hint.
 *
 *  This variable can be set to the following values:
 *    "0"       - Enable idle timer
 *    "1"       - Disable idle timer
 */
#define SDL_HINT_IDLE_TIMER_DISABLED "SDL_IOS_IDLE_TIMER_DISABLED"

/**
 * \brief A variable to control whether certain IMEs should handle text editing internally instead of sending SDL_TEXTEDITING events.
 *
 * The variable can be set to the following values:
 *   "0"       - SDL_TEXTEDITING events are sent, and it is the application's
 *               responsibility to render the text from these events and 
 *               differentiate it somehow from committed text. (default)
 *   "1"       - If supported by the IME then SDL_TEXTEDITING events are not sent, 
 *               and text that is being composed will be rendered in its own UI.
 */
#define SDL_HINT_IME_INTERNAL_EDITING "SDL_IME_INTERNAL_EDITING"

/**
 * \brief A variable to control whether certain IMEs should show native UI components (such as the Candidate List) instead of suppressing them.
 *
 * The variable can be set to the following values:
 *   "0"       - Native UI components are not display. (default)
 *   "1"       - Native UI components are displayed.
 */
#define SDL_HINT_IME_SHOW_UI "SDL_IME_SHOW_UI"

/**
 * \brief  A variable controlling whether the home indicator bar on iPhone X
 *         should be hidden.
 *
 *  This variable can be set to the following values:
 *    "0"       - The indicator bar is not hidden (default for windowed applications)
 *    "1"       - The indicator bar is hidden and is shown when the screen is touched (useful for movie playback applications)
 *    "2"       - The indicator bar is dim and the first swipe makes it visible and the second swipe performs the "home" action (default for fullscreen applications)
 */
#define SDL_HINT_IOS_HIDE_HOME_INDICATOR "SDL_IOS_HIDE_HOME_INDICATOR"

/**
 *  \brief  A variable that lets you enable joystick (and gamecontroller) events even when your app is in the background.
 *
 *  The variable can be set to the following values:
 *    "0"       - Disable joystick & gamecontroller input events when the
 *                application is in the background.
 *    "1"       - Enable joystick & gamecontroller input events when the
 *                application is in the background.
 *
 *  The default value is "0".  This hint may be set at any time.
 */
#define SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"

/**
 *  \brief  A variable controlling whether the HIDAPI joystick drivers should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - HIDAPI drivers are not used
 *    "1"       - HIDAPI drivers are used (the default)
 *
 *  This variable is the default for all drivers, but can be overridden by the hints for specific drivers below.
 */
#define SDL_HINT_JOYSTICK_HIDAPI "SDL_JOYSTICK_HIDAPI"

/**
 *  \brief  A variable controlling whether the HIDAPI driver for Nintendo GameCube controllers should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - HIDAPI driver is not used
 *    "1"       - HIDAPI driver is used
 *
 *  The default is the value of SDL_HINT_JOYSTICK_HIDAPI
 */
#define SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE "SDL_JOYSTICK_HIDAPI_GAMECUBE"

 /**
  *  \brief  A variable controlling whether Switch Joy-Cons should be treated the same as Switch Pro Controllers when using the HIDAPI driver.
  *
  *  This variable can be set to the following values:
  *    "0"       - basic Joy-Con support with no analog input (the default)
  *    "1"       - Joy-Cons treated as half full Pro Controllers with analog inputs and sensors
  *
  *  This does not combine Joy-Cons into a single controller. That's up to the user.
  */
#define SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS "SDL_JOYSTICK_HIDAPI_JOY_CONS"

 /**
  *  \brief  A variable controlling whether the HIDAPI driver for Amazon Luna controllers connected via Bluetooth should be used.
  *
  *  This variable can be set to the following values:
  *    "0"       - HIDAPI driver is not used
  *    "1"       - HIDAPI driver is used
  *
  *  The default is the value of SDL_HINT_JOYSTICK_HIDAPI
  */
#define SDL_HINT_JOYSTICK_HIDAPI_LUNA "SDL_JOYSTICK_HIDAPI_LUNA"

/**
 *  \brief  A variable controlling whether the HIDAPI driver for PS4 controllers should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - HIDAPI driver is not used
 *    "1"       - HIDAPI driver is used
 *
 *  The default is the value of SDL_HINT_JOYSTICK_HIDAPI
 */
#define SDL_HINT_JOYSTICK_HIDAPI_PS4 "SDL_JOYSTICK_HIDAPI_PS4"

/**
 *  \brief  A variable controlling whether extended input reports should be used for PS4 controllers when using the HIDAPI driver.
 *
 *  This variable can be set to the following values:
 *    "0"       - extended reports are not enabled (the default)
 *    "1"       - extended reports
 *
 *  Extended input reports allow rumble on Bluetooth PS4 controllers, but
 *  break DirectInput handling for applications that don't use SDL.
 *
 *  Once extended reports are enabled, they can not be disabled without
 *  power cycling the controller.
 *
 *  For compatibility with applications written for versions of SDL prior
 *  to the introduction of PS5 controller support, this value will also
 *  control the state of extended reports on PS5 controllers when the
 *  SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE hint is not explicitly set.
 */
#define SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE "SDL_JOYSTICK_HIDAPI_PS4_RUMBLE"

/**
 *  \brief  A variable controlling whether the HIDAPI driver for PS5 controllers should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - HIDAPI driver is not used
 *    "1"       - HIDAPI driver is used
 *
 *  The default is the value of SDL_HINT_JOYSTICK_HIDAPI
 */
#define SDL_HINT_JOYSTICK_HIDAPI_PS5 "SDL_JOYSTICK_HIDAPI_PS5"

/**
 *  \brief  A variable controlling whether the player LEDs should be lit to indicate which player is associated with a PS5 controller.
 *
 *  This variable can be set to the following values:
 *    "0"       - player LEDs are not enabled
 *    "1"       - player LEDs are enabled (the default)
 */
#define SDL_HINT_JOYSTICK_HIDAPI_PS5_PLAYER_LED "SDL_JOYSTICK_HIDAPI_PS5_PLAYER_LED"

/**
 *  \brief  A variable controlling whether extended input reports should be used for PS5 controllers when using the HIDAPI driver.
 *
 *  This variable can be set to the following values:
 *    "0"       - extended reports are not enabled (the default)
 *    "1"       - extended reports
 *
 *  Extended input reports allow rumble on Bluetooth PS5 controllers, but
 *  break DirectInput handling for applications that don't use SDL.
 *
 *  Once extended reports are enabled, they can not be disabled without
 *  power cycling the controller.
 *
 *  For compatibility with applications written for versions of SDL prior
 *  to the introduction of PS5 controller support, this value defaults to
 *  the value of SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE.
 */
#define SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE "SDL_JOYSTICK_HIDAPI_PS5_RUMBLE"

/**
 *  \brief  A variable controlling whether the HIDAPI driver for Google Stadia controllers should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - HIDAPI driver is not used
 *    "1"       - HIDAPI driver is used
 *
 *  The default is the value of SDL_HINT_JOYSTICK_HIDAPI
 */
#define SDL_HINT_JOYSTICK_HIDAPI_STADIA "SDL_JOYSTICK_HIDAPI_STADIA"

/**
 *  \brief  A variable controlling whether the HIDAPI driver for Steam Controllers should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - HIDAPI driver is not used
 *    "1"       - HIDAPI driver is used for Steam Controllers, which requires Bluetooth access
 *                and may prompt the user for permission on iOS and Android.
 *
 *  The default is "0"
 */
#define SDL_HINT_JOYSTICK_HIDAPI_STEAM "SDL_JOYSTICK_HIDAPI_STEAM"

/**
 *  \brief  A variable controlling whether the HIDAPI driver for Nintendo Switch controllers should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - HIDAPI driver is not used
 *    "1"       - HIDAPI driver is used
 *
 *  The default is the value of SDL_HINT_JOYSTICK_HIDAPI
 */
#define SDL_HINT_JOYSTICK_HIDAPI_SWITCH "SDL_JOYSTICK_HIDAPI_SWITCH"

/**
 *  \brief  A variable controlling whether the Home button LED should be turned on when a Nintendo Switch controller is opened
 *
 *  This variable can be set to the following values:
 *    "0"       - home button LED is turned off
 *    "1"       - home button LED is turned on
 *
 *  By default the Home button LED state is not changed.
 */
#define SDL_HINT_JOYSTICK_HIDAPI_SWITCH_HOME_LED "SDL_JOYSTICK_HIDAPI_SWITCH_HOME_LED"

/**
 *  \brief  A variable controlling whether the HIDAPI driver for XBox controllers should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - HIDAPI driver is not used
 *    "1"       - HIDAPI driver is used
 *
 *  The default is "0" on Windows, otherwise the value of SDL_HINT_JOYSTICK_HIDAPI
 */
#define SDL_HINT_JOYSTICK_HIDAPI_XBOX   "SDL_JOYSTICK_HIDAPI_XBOX"

 /**
  *  \brief  A variable controlling whether the RAWINPUT joystick drivers should be used for better handling XInput-capable devices.
  *
  *  This variable can be set to the following values:
  *    "0"       - RAWINPUT drivers are not used
  *    "1"       - RAWINPUT drivers are used (the default)
  *
  */
#define SDL_HINT_JOYSTICK_RAWINPUT "SDL_JOYSTICK_RAWINPUT"

 /**
  *  \brief  A variable controlling whether the RAWINPUT driver should pull correlated data from XInput.
  *
  *  This variable can be set to the following values:
  *    "0"       - RAWINPUT driver will only use data from raw input APIs
  *    "1"       - RAWINPUT driver will also pull data from XInput, providing
  *                better trigger axes, guide button presses, and rumble support
  *                for Xbox controllers
  *
  *  The default is "1".  This hint applies to any joysticks opened after setting the hint.
  */
#define SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT   "SDL_JOYSTICK_RAWINPUT_CORRELATE_XINPUT"

 /**
  *  \brief  A variable controlling whether a separate thread should be used
  *          for handling joystick detection and raw input messages on Windows
  *
  *  This variable can be set to the following values:
  *    "0"       - A separate thread is not used (the default)
  *    "1"       - A separate thread is used for handling raw input messages
  *
  */
#define SDL_HINT_JOYSTICK_THREAD "SDL_JOYSTICK_THREAD"

/**
 * \brief Determines whether SDL enforces that DRM master is required in order
 *        to initialize the KMSDRM video backend.
 *
 * The DRM subsystem has a concept of a "DRM master" which is a DRM client that
 * has the ability to set planes, set cursor, etc. When SDL is DRM master, it
 * can draw to the screen using the SDL rendering APIs. Without DRM master, SDL
 * is still able to process input and query attributes of attached displays,
 * but it cannot change display state or draw to the screen directly.
 *
 * In some cases, it can be useful to have the KMSDRM backend even if it cannot
 * be used for rendering. An app may want to use SDL for input processing while
 * using another rendering API (such as an MMAL overlay on Raspberry Pi) or
 * using its own code to render to DRM overlays that SDL doesn't support.
 *
 * This hint must be set before initializing the video subsystem.
 *
 * This variable can be set to the following values:
 *    "0"       - SDL will allow usage of the KMSDRM backend without DRM master
 *    "1"       - SDL Will require DRM master to use the KMSDRM backend (default)
 */
#define SDL_HINT_KMSDRM_REQUIRE_DRM_MASTER      "SDL_KMSDRM_REQUIRE_DRM_MASTER"

 /**
  *  \brief  A comma separated list of devices to open as joysticks
  *
  *  This variable is currently only used by the Linux joystick driver.
  */
#define SDL_HINT_JOYSTICK_DEVICE "SDL_JOYSTICK_DEVICE"

 /**
  *  \brief  A variable controlling whether to use the classic /dev/input/js* joystick interface or the newer /dev/input/event* joystick interface on Linux
  *
  *  This variable can be set to the following values:
  *    "0"       - Use /dev/input/event*
  *    "1"       - Use /dev/input/js*
  *
  *  By default the /dev/input/event* interfaces are used
  */
#define SDL_HINT_LINUX_JOYSTICK_CLASSIC "SDL_LINUX_JOYSTICK_CLASSIC"

 /**
  *  \brief  A variable controlling whether joysticks on Linux adhere to their HID-defined deadzones or return unfiltered values.
  *
  *  This variable can be set to the following values:
  *    "0"       - Return unfiltered joystick axis values (the default)
  *    "1"       - Return axis values with deadzones taken into account
  */
#define SDL_HINT_LINUX_JOYSTICK_DEADZONES "SDL_LINUX_JOYSTICK_DEADZONES"

/**
*  \brief  When set don't force the SDL app to become a foreground process
*
*  This hint only applies to Mac OS X.
*
*/
#define SDL_HINT_MAC_BACKGROUND_APP    "SDL_MAC_BACKGROUND_APP"

/**
 *  \brief A variable that determines whether ctrl+click should generate a right-click event on Mac
 *
 *  If present, holding ctrl while left clicking will generate a right click
 *  event when on Mac.
 */
#define SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK "SDL_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK"

/**
 *  \brief  A variable setting the double click radius, in pixels.
 */
#define SDL_HINT_MOUSE_DOUBLE_CLICK_RADIUS    "SDL_MOUSE_DOUBLE_CLICK_RADIUS"

/**
 *  \brief  A variable setting the double click time, in milliseconds.
 */
#define SDL_HINT_MOUSE_DOUBLE_CLICK_TIME    "SDL_MOUSE_DOUBLE_CLICK_TIME"

/**
 *  \brief Allow mouse click events when clicking to focus an SDL window
 *
 *  This variable can be set to the following values:
 *    "0"       - Ignore mouse clicks that activate a window
 *    "1"       - Generate events for mouse clicks that activate a window
 *
 *  By default SDL will ignore mouse clicks that activate a window
 */
#define SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH "SDL_MOUSE_FOCUS_CLICKTHROUGH"

/**
 *  \brief  A variable setting the speed scale for mouse motion, in floating point, when the mouse is not in relative mode
 */
#define SDL_HINT_MOUSE_NORMAL_SPEED_SCALE    "SDL_MOUSE_NORMAL_SPEED_SCALE"

/**
 *  \brief  A variable controlling whether relative mouse mode is implemented using mouse warping
 *
 *  This variable can be set to the following values:
 *    "0"       - Relative mouse mode uses raw input
 *    "1"       - Relative mouse mode uses mouse warping
 *
 *  By default SDL will use raw input for relative mouse mode
 */
#define SDL_HINT_MOUSE_RELATIVE_MODE_WARP    "SDL_MOUSE_RELATIVE_MODE_WARP"

/**
 *  \brief  A variable controlling whether relative mouse motion is affected by renderer scaling
 *
 *  This variable can be set to the following values:
 *    "0"       - Relative motion is unaffected by DPI or renderer's logical size
 *    "1"       - Relative motion is scaled according to DPI scaling and logical size
 *
 *  By default relative mouse deltas are affected by DPI and renderer scaling
 */
#define SDL_HINT_MOUSE_RELATIVE_SCALING "SDL_MOUSE_RELATIVE_SCALING"

/**
 *  \brief  A variable setting the scale for mouse motion, in floating point, when the mouse is in relative mode
 */
#define SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE    "SDL_MOUSE_RELATIVE_SPEED_SCALE"

/**
 *  \brief  A variable controlling whether mouse events should generate synthetic touch events
 *
 *  This variable can be set to the following values:
 *    "0"       - Mouse events will not generate touch events (default for desktop platforms)
 *    "1"       - Mouse events will generate touch events (default for mobile platforms, such as Android and iOS)
 */
#define SDL_HINT_MOUSE_TOUCH_EVENTS    "SDL_MOUSE_TOUCH_EVENTS"

/**
 *  \brief Tell SDL not to catch the SIGINT or SIGTERM signals.
 *
 * This hint only applies to Unix-like platforms, and should set before
 * any calls to SDL_Init()
 *
 * The variable can be set to the following values:
 *   "0"       - SDL will install a SIGINT and SIGTERM handler, and when it
 *               catches a signal, convert it into an SDL_QUIT event.
 *   "1"       - SDL will not install a signal handler at all.
 */
#define SDL_HINT_NO_SIGNAL_HANDLERS   "SDL_NO_SIGNAL_HANDLERS"

/**
 *  \brief  A variable controlling what driver to use for OpenGL ES contexts.
 *
 *  On some platforms, currently Windows and X11, OpenGL drivers may support
 *  creating contexts with an OpenGL ES profile. By default SDL uses these
 *  profiles, when available, otherwise it attempts to load an OpenGL ES
 *  library, e.g. that provided by the ANGLE project. This variable controls
 *  whether SDL follows this default behaviour or will always load an
 *  OpenGL ES library.
 *
 *  Circumstances where this is useful include
 *  - Testing an app with a particular OpenGL ES implementation, e.g ANGLE,
 *    or emulator, e.g. those from ARM, Imagination or Qualcomm.
 *  - Resolving OpenGL ES function addresses at link time by linking with
 *    the OpenGL ES library instead of querying them at run time with
 *    SDL_GL_GetProcAddress().
 *
 *  Caution: for an application to work with the default behaviour across
 *  different OpenGL drivers it must query the OpenGL ES function
 *  addresses at run time using SDL_GL_GetProcAddress().
 *
 *  This variable is ignored on most platforms because OpenGL ES is native
 *  or not supported.
 *
 *  This variable can be set to the following values:
 *    "0"       - Use ES profile of OpenGL, if available. (Default when not set.)
 *    "1"       - Load OpenGL ES library using the default library names.
 *
 */
#define SDL_HINT_OPENGL_ES_DRIVER   "SDL_OPENGL_ES_DRIVER"

/**
 *  \brief  A variable controlling which orientations are allowed on iOS/Android.
 *
 *  In some circumstances it is necessary to be able to explicitly control
 *  which UI orientations are allowed.
 *
 *  This variable is a space delimited list of the following values:
 *    "LandscapeLeft", "LandscapeRight", "Portrait" "PortraitUpsideDown"
 */
#define SDL_HINT_ORIENTATIONS "SDL_IOS_ORIENTATIONS"

/**
 *  \brief  A variable controlling the use of a sentinel event when polling the event queue
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable poll sentinels
 *    "1"       - Enable poll sentinels
 *
 *  When polling for events, SDL_PumpEvents is used to gather new events from devices.
 *  If a device keeps producing new events between calls to SDL_PumpEvents, a poll loop will
 *  become stuck until the new events stop.
 *  This is most noticable when moving a high frequency mouse.
 *
 *  By default, poll sentinels are enabled.
 */
#define SDL_HINT_POLL_SENTINEL "SDL_POLL_SENTINEL"

/**
 *  \brief Override for SDL_GetPreferredLocales()
 *
 *  If set, this will be favored over anything the OS might report for the
 *  user's preferred locales. Changing this hint at runtime will not generate
 *  a SDL_LOCALECHANGED event (but if you can change the hint, you can push
 *  your own event, if you want).
 *
 *  The format of this hint is a comma-separated list of language and locale,
 *  combined with an underscore, as is a common format: "en_GB". Locale is
 *  optional: "en". So you might have a list like this: "en_GB,jp,es_PT"
 */
#define SDL_HINT_PREFERRED_LOCALES "SDL_PREFERRED_LOCALES"

/**
 *  \brief  A variable describing the content orientation on QtWayland-based platforms.
 *
 *  On QtWayland platforms, windows are rotated client-side to allow for custom
 *  transitions. In order to correctly position overlays (e.g. volume bar) and
 *  gestures (e.g. events view, close/minimize gestures), the system needs to
 *  know in which orientation the application is currently drawing its contents.
 *
 *  This does not cause the window to be rotated or resized, the application
 *  needs to take care of drawing the content in the right orientation (the
 *  framebuffer is always in portrait mode).
 *
 *  This variable can be one of the following values:
 *    "primary" (default), "portrait", "landscape", "inverted-portrait", "inverted-landscape"
 */
#define SDL_HINT_QTWAYLAND_CONTENT_ORIENTATION "SDL_QTWAYLAND_CONTENT_ORIENTATION"

/**
 *  \brief  Flags to set on QtWayland windows to integrate with the native window manager.
 *
 *  On QtWayland platforms, this hint controls the flags to set on the windows.
 *  For example, on Sailfish OS "OverridesSystemGestures" disables swipe gestures.
 *
 *  This variable is a space-separated list of the following values (empty = no flags):
 *    "OverridesSystemGestures", "StaysOnTop", "BypassWindowManager"
 */
#define SDL_HINT_QTWAYLAND_WINDOW_FLAGS "SDL_QTWAYLAND_WINDOW_FLAGS"

/**
 *  \brief  A variable controlling whether the 2D render API is compatible or efficient.
 *
 *  This variable can be set to the following values:
 *
 *    "0"     - Don't use batching to make rendering more efficient.
 *    "1"     - Use batching, but might cause problems if app makes its own direct OpenGL calls.
 *
 *  Up to SDL 2.0.9, the render API would draw immediately when requested. Now
 *  it batches up draw requests and sends them all to the GPU only when forced
 *  to (during SDL_RenderPresent, when changing render targets, by updating a
 *  texture that the batch needs, etc). This is significantly more efficient,
 *  but it can cause problems for apps that expect to render on top of the
 *  render API's output. As such, SDL will disable batching if a specific
 *  render backend is requested (since this might indicate that the app is
 *  planning to use the underlying graphics API directly). This hint can
 *  be used to explicitly request batching in this instance. It is a contract
 *  that you will either never use the underlying graphics API directly, or
 *  if you do, you will call SDL_RenderFlush() before you do so any current
 *  batch goes to the GPU before your work begins. Not following this contract
 *  will result in undefined behavior.
 */
#define SDL_HINT_RENDER_BATCHING  "SDL_RENDER_BATCHING"

/**
 *  \brief  A variable controlling whether to enable Direct3D 11+'s Debug Layer.
 *
 *  This variable does not have any effect on the Direct3D 9 based renderer.
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable Debug Layer use
 *    "1"       - Enable Debug Layer use
 *
 *  By default, SDL does not use Direct3D Debug Layer.
 */
#define SDL_HINT_RENDER_DIRECT3D11_DEBUG    "SDL_RENDER_DIRECT3D11_DEBUG"

/**
 *  \brief  A variable controlling whether the Direct3D device is initialized for thread-safe operations.
 *
 *  This variable can be set to the following values:
 *    "0"       - Thread-safety is not enabled (faster)
 *    "1"       - Thread-safety is enabled
 *
 *  By default the Direct3D device is created with thread-safety disabled.
 */
#define SDL_HINT_RENDER_DIRECT3D_THREADSAFE "SDL_RENDER_DIRECT3D_THREADSAFE"

/**
 *  \brief  A variable specifying which render driver to use.
 *
 *  If the application doesn't pick a specific renderer to use, this variable
 *  specifies the name of the preferred renderer.  If the preferred renderer
 *  can't be initialized, the normal default renderer is used.
 *
 *  This variable is case insensitive and can be set to the following values:
 *    "direct3d"
 *    "opengl"
 *    "opengles2"
 *    "opengles"
 *    "metal"
 *    "software"
 *
 *  The default varies by platform, but it's the first one in the list that
 *  is available on the current platform.
 */
#define SDL_HINT_RENDER_DRIVER              "SDL_RENDER_DRIVER"

/**
 *  \brief  A variable controlling the scaling policy for SDL_RenderSetLogicalSize.
 *
 *  This variable can be set to the following values:
 *    "0" or "letterbox" - Uses letterbox/sidebars to fit the entire rendering on screen
 *    "1" or "overscan"  - Will zoom the rendering so it fills the entire screen, allowing edges to be drawn offscreen
 *
 *  By default letterbox is used
 */
#define SDL_HINT_RENDER_LOGICAL_SIZE_MODE       "SDL_RENDER_LOGICAL_SIZE_MODE"

/**
 *  \brief  A variable controlling whether the OpenGL render driver uses shaders if they are available.
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable shaders
 *    "1"       - Enable shaders
 *
 *  By default shaders are used if OpenGL supports them.
 */
#define SDL_HINT_RENDER_OPENGL_SHADERS      "SDL_RENDER_OPENGL_SHADERS"

/**
 *  \brief  A variable controlling the scaling quality
 *
 *  This variable can be set to the following values:
 *    "0" or "nearest" - Nearest pixel sampling
 *    "1" or "linear"  - Linear filtering (supported by OpenGL and Direct3D)
 *    "2" or "best"    - Currently this is the same as "linear"
 *
 *  By default nearest pixel sampling is used
 */
#define SDL_HINT_RENDER_SCALE_QUALITY       "SDL_RENDER_SCALE_QUALITY"

/**
 *  \brief  A variable controlling whether updates to the SDL screen surface should be synchronized with the vertical refresh, to avoid tearing.
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable vsync
 *    "1"       - Enable vsync
 *
 *  By default SDL does not sync screen surface updates with vertical refresh.
 */
#define SDL_HINT_RENDER_VSYNC               "SDL_RENDER_VSYNC"

 /**
 * \brief A variable to control whether the return key on the soft keyboard
 *        should hide the soft keyboard on Android and iOS.
 *
 * The variable can be set to the following values:
 *   "0"       - The return key will be handled as a key event. This is the behaviour of SDL <= 2.0.3. (default)
 *   "1"       - The return key will hide the keyboard.
 *
 * The value of this hint is used at runtime, so it can be changed at any time.
 */
#define SDL_HINT_RETURN_KEY_HIDES_IME "SDL_RETURN_KEY_HIDES_IME"

/**
 * \brief Tell SDL which Dispmanx layer to use on a Raspberry PI
 *
 * Also known as Z-order. The variable can take a negative or positive value.
 * The default is 10000.
 */
#define SDL_HINT_RPI_VIDEO_LAYER           "SDL_RPI_VIDEO_LAYER"

/**
 *  \brief Specify an "activity name" for screensaver inhibition.
 *
 * Some platforms, notably Linux desktops, list the applications which are
 * inhibiting the screensaver or other power-saving features.
 *
 * This hint lets you specify the "activity name" sent to the OS when
 * SDL_DisableScreenSaver() is used (or the screensaver is automatically
 * disabled). The contents of this hint are used when the screensaver is
 * disabled. You should use a string that describes what your program is doing
 * (and, therefore, why the screensaver is disabled).  For example, "Playing a
 * game" or "Watching a video".
 * 
 * Setting this to "" or leaving it unset will have SDL use a reasonable
 * default: "Playing a game" or something similar.
 *
 * On targets where this is not supported, this hint does nothing.
 */
#define SDL_HINT_SCREENSAVER_INHIBIT_ACTIVITY_NAME "SDL_SCREENSAVER_INHIBIT_ACTIVITY_NAME"

/**
 *  \brief Specifies whether SDL_THREAD_PRIORITY_TIME_CRITICAL should be treated as realtime.
 *
 *  On some platforms, like Linux, a realtime priority thread may be subject to restrictions
 *  that require special handling by the application. This hint exists to let SDL know that
 *  the app is prepared to handle said restrictions.
 * 
 *  On Linux, SDL will apply the following configuration to any thread that becomes realtime:
 *   * The SCHED_RESET_ON_FORK bit will be set on the scheduling policy,
 *   * An RLIMIT_RTTIME budget will be configured to the rtkit specified limit.
 *     * Exceeding this limit will result in the kernel sending SIGKILL to the app,
 *     * Refer to the man pages for more information.
 * 
 *  This variable can be set to the following values:
 *    "0"       - default platform specific behaviour
 *    "1"       - Force SDL_THREAD_PRIORITY_TIME_CRITICAL to a realtime scheduling policy
 */
#define SDL_HINT_THREAD_FORCE_REALTIME_TIME_CRITICAL "SDL_THREAD_FORCE_REALTIME_TIME_CRITICAL"

/**
*  \brief  A string specifying additional information to use with SDL_SetThreadPriority.
*
*  By default SDL_SetThreadPriority will make appropriate system changes in order to
*  apply a thread priority.  For example on systems using pthreads the scheduler policy
*  is changed automatically to a policy that works well with a given priority.
*  Code which has specific requirements can override SDL's default behavior with this hint.
*
*  pthread hint values are "current", "other", "fifo" and "rr".
*  Currently no other platform hint values are defined but may be in the future.
*
*  \note On Linux, the kernel may send SIGKILL to realtime tasks which exceed the distro
*  configured execution budget for rtkit. This budget can be queried through RLIMIT_RTTIME
*  after calling SDL_SetThreadPriority().
*/
#define SDL_HINT_THREAD_PRIORITY_POLICY         "SDL_THREAD_PRIORITY_POLICY"

/**
*  \brief  A string specifying SDL's threads stack size in bytes or "0" for the backend's default size
*
*  Use this hint in case you need to set SDL's threads stack size to other than the default.
*  This is specially useful if you build SDL against a non glibc libc library (such as musl) which
*  provides a relatively small default thread stack size (a few kilobytes versus the default 8MB glibc uses).
*  Support for this hint is currently available only in the pthread, Windows, and PSP backend.
*
*  Instead of this hint, in 2.0.9 and later, you can use
*  SDL_CreateThreadWithStackSize(). This hint only works with the classic
*  SDL_CreateThread().
*/
#define SDL_HINT_THREAD_STACK_SIZE              "SDL_THREAD_STACK_SIZE"

/**
 *  \brief A variable that controls the timer resolution, in milliseconds.
 *
 *  The higher resolution the timer, the more frequently the CPU services
 *  timer interrupts, and the more precise delays are, but this takes up
 *  power and CPU time.  This hint is only used on Windows.
 *
 *  See this blog post for more information:
 *  http://randomascii.wordpress.com/2013/07/08/windows-timer-resolution-megawatts-wasted/
 *
 *  If this variable is set to "0", the system timer resolution is not set.
 *
 *  The default value is "1". This hint may be set at any time.
 */
#define SDL_HINT_TIMER_RESOLUTION "SDL_TIMER_RESOLUTION"

/**
 *  \brief  A variable controlling whether touch events should generate synthetic mouse events
 *
 *  This variable can be set to the following values:
 *    "0"       - Touch events will not generate mouse events
 *    "1"       - Touch events will generate mouse events
 *
 *  By default SDL will generate mouse events for touch events
 */
#define SDL_HINT_TOUCH_MOUSE_EVENTS    "SDL_TOUCH_MOUSE_EVENTS"

/**
 *  \brief  A variable controlling whether the Android / tvOS remotes
 *  should be listed as joystick devices, instead of sending keyboard events.
 *
 *  This variable can be set to the following values:
 *    "0"       - Remotes send enter/escape/arrow key events
 *    "1"       - Remotes are available as 2 axis, 2 button joysticks (the default).
 */
#define SDL_HINT_TV_REMOTE_AS_JOYSTICK "SDL_TV_REMOTE_AS_JOYSTICK"

/**
 *  \brief  A variable controlling whether the screensaver is enabled. 
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable screensaver
 *    "1"       - Enable screensaver
 *
 *  By default SDL will disable the screensaver.
 */
#define SDL_HINT_VIDEO_ALLOW_SCREENSAVER    "SDL_VIDEO_ALLOW_SCREENSAVER"

/**
 * \brief Tell the video driver that we only want a double buffer.
 *
 * By default, most lowlevel 2D APIs will use a triple buffer scheme that 
 * wastes no CPU time on waiting for vsync after issuing a flip, but
 * introduces a frame of latency. On the other hand, using a double buffer
 * scheme instead is recommended for cases where low latency is an important
 * factor because we save a whole frame of latency.
 * We do so by waiting for vsync immediately after issuing a flip, usually just
 * after eglSwapBuffers call in the backend's *_SwapWindow function.
 *
 * Since it's driver-specific, it's only supported where possible and
 * implemented. Currently supported the following drivers:
 *
 * - KMSDRM (kmsdrm)
 * - Raspberry Pi (raspberrypi)
 */
#define SDL_HINT_VIDEO_DOUBLE_BUFFER      "SDL_VIDEO_DOUBLE_BUFFER"

/**
 * \brief A variable controlling whether the EGL window is allowed to be
 * composited as transparent, rather than opaque.
 *
 * Most window systems will always render windows opaque, even if the surface
 * format has an alpha channel. This is not always true, however, so by default
 * SDL will try to enforce opaque composition. To override this behavior, you
 * can set this hint to "1".
 */
#define SDL_HINT_VIDEO_EGL_ALLOW_TRANSPARENCY "SDL_VIDEO_EGL_ALLOW_TRANSPARENCY"

/**
 * \brief A variable controlling whether the graphics context is externally managed.
 *
 * This variable can be set to the following values:
 *  "0"         - SDL will manage graphics contexts that are attached to windows.
 *  "1"         - Disable graphics context management on windows.
 *
 * By default SDL will manage OpenGL contexts in certain situations. For example, on Android the
 * context will be automatically saved and restored when pausing the application. Additionally, some
 * platforms will assume usage of OpenGL if Vulkan isn't used. Setting this to "1" will prevent this
 * behavior, which is desireable when the application manages the graphics context, such as
 * an externally managed OpenGL context or attaching a Vulkan surface to the window.
 */
#define SDL_HINT_VIDEO_EXTERNAL_CONTEXT    "SDL_VIDEO_EXTERNAL_CONTEXT"

/**
 *  \brief If set to 1, then do not allow high-DPI windows. ("Retina" on Mac and iOS)
 */
#define SDL_HINT_VIDEO_HIGHDPI_DISABLED "SDL_VIDEO_HIGHDPI_DISABLED"

/**
 *  \brief  A variable that dictates policy for fullscreen Spaces on Mac OS X.
 *
 *  This hint only applies to Mac OS X.
 *
 *  The variable can be set to the following values:
 *    "0"       - Disable Spaces support (FULLSCREEN_DESKTOP won't use them and
 *                SDL_WINDOW_RESIZABLE windows won't offer the "fullscreen"
 *                button on their titlebars).
 *    "1"       - Enable Spaces support (FULLSCREEN_DESKTOP will use them and
 *                SDL_WINDOW_RESIZABLE windows will offer the "fullscreen"
 *                button on their titlebars).
 *
 *  The default value is "1". Spaces are disabled regardless of this hint if
 *   the OS isn't at least Mac OS X Lion (10.7). This hint must be set before
 *   any windows are created.
 */
#define SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES    "SDL_VIDEO_MAC_FULLSCREEN_SPACES"

/**
 *  \brief Minimize your SDL_Window if it loses key focus when in fullscreen mode. Defaults to false.
 *  \warning  Before SDL 2.0.14, this defaulted to true! In 2.0.14, we're
 *            seeing if "true" causes more problems than it solves in modern times.
 *
 */
#define SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS   "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS"

/**
 *  \brief  A variable controlling whether the libdecor Wayland backend is allowed to be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - libdecor use is disabled.
 *    "1"       - libdecor use is enabled (default).
 *
 *  libdecor is used over xdg-shell when xdg-decoration protocol is unavailable.
 */
#define SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR "SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR"

/**
*  \brief  A variable that is the address of another SDL_Window* (as a hex string formatted with "%p").
*  
*  If this hint is set before SDL_CreateWindowFrom() and the SDL_Window* it is set to has
*  SDL_WINDOW_OPENGL set (and running on WGL only, currently), then two things will occur on the newly 
*  created SDL_Window:
*
*  1. Its pixel format will be set to the same pixel format as this SDL_Window.  This is
*  needed for example when sharing an OpenGL context across multiple windows.
*
*  2. The flag SDL_WINDOW_OPENGL will be set on the new window so it can be used for
*  OpenGL rendering.
*
*  This variable can be set to the following values:
*    The address (as a string "%p") of the SDL_Window* that new windows created with SDL_CreateWindowFrom() should
*    share a pixel format with.
*/
#define SDL_HINT_VIDEO_WINDOW_SHARE_PIXEL_FORMAT    "SDL_VIDEO_WINDOW_SHARE_PIXEL_FORMAT"

/**
*  \brief  A variable specifying which shader compiler to preload when using the Chrome ANGLE binaries
*
*  SDL has EGL and OpenGL ES2 support on Windows via the ANGLE project. It
*  can use two different sets of binaries, those compiled by the user from source
*  or those provided by the Chrome browser. In the later case, these binaries require
*  that SDL loads a DLL providing the shader compiler.
*
*  This variable can be set to the following values:
*    "d3dcompiler_46.dll" - default, best for Vista or later.
*    "d3dcompiler_43.dll" - for XP support.
*    "none" - do not load any library, useful if you compiled ANGLE from source and included the compiler in your binaries.
*
*/
#define SDL_HINT_VIDEO_WIN_D3DCOMPILER              "SDL_VIDEO_WIN_D3DCOMPILER"

/**
 * \brief A variable controlling whether X11 should use GLX or EGL by default
 *
 * This variable can be set to the following values:
 * "0" - Use GLX
 * "1" - Use EGL
 *
 * By default SDL will use GLX when both are present.
 */
#define SDL_HINT_VIDEO_X11_FORCE_EGL "SDL_VIDEO_X11_FORCE_EGL"

/**
 * \brief A variable controlling whether the X11 _NET_WM_BYPASS_COMPOSITOR hint should be used.
 * 
 * This variable can be set to the following values:
 * "0" - Disable _NET_WM_BYPASS_COMPOSITOR
 * "1" - Enable _NET_WM_BYPASS_COMPOSITOR
 * 
 * By default SDL will use _NET_WM_BYPASS_COMPOSITOR
 * 
 */
#define SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR "SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR"

/**
 *  \brief  A variable controlling whether the X11 _NET_WM_PING protocol should be supported.
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable _NET_WM_PING
 *    "1"       - Enable _NET_WM_PING
 *
 *  By default SDL will use _NET_WM_PING, but for applications that know they
 *  will not always be able to respond to ping requests in a timely manner they can
 *  turn it off to avoid the window manager thinking the app is hung.
 *  The hint is checked in CreateWindow.
 */
#define SDL_HINT_VIDEO_X11_NET_WM_PING      "SDL_VIDEO_X11_NET_WM_PING"

/**
 *  \brief  A variable forcing the visual ID chosen for new X11 windows
 *
 */
#define SDL_HINT_VIDEO_X11_WINDOW_VISUALID      "SDL_VIDEO_X11_WINDOW_VISUALID"

/**
 *  \brief  A variable controlling whether the X11 Xinerama extension should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable Xinerama
 *    "1"       - Enable Xinerama
 *
 *  By default SDL will use Xinerama if it is available.
 */
#define SDL_HINT_VIDEO_X11_XINERAMA         "SDL_VIDEO_X11_XINERAMA"

/**
 *  \brief  A variable controlling whether the X11 XRandR extension should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable XRandR
 *    "1"       - Enable XRandR
 *
 *  By default SDL will not use XRandR because of window manager issues.
 */
#define SDL_HINT_VIDEO_X11_XRANDR           "SDL_VIDEO_X11_XRANDR"

/**
 *  \brief  A variable controlling whether the X11 VidMode extension should be used.
 *
 *  This variable can be set to the following values:
 *    "0"       - Disable XVidMode
 *    "1"       - Enable XVidMode
 *
 *  By default SDL will use XVidMode if it is available.
 */
#define SDL_HINT_VIDEO_X11_XVIDMODE         "SDL_VIDEO_X11_XVIDMODE"

/**
 *  \brief  Controls how the fact chunk affects the loading of a WAVE file.
 *
 *  The fact chunk stores information about the number of samples of a WAVE
 *  file. The Standards Update from Microsoft notes that this value can be used
 *  to 'determine the length of the data in seconds'. This is especially useful
 *  for compressed formats (for which this is a mandatory chunk) if they produce
 *  multiple sample frames per block and truncating the block is not allowed.
 *  The fact chunk can exactly specify how many sample frames there should be
 *  in this case.
 *
 *  Unfortunately, most application seem to ignore the fact chunk and so SDL
 *  ignores it by default as well.
 *
 *  This variable can be set to the following values:
 *
 *    "truncate"    - Use the number of samples to truncate the wave data if
 *                    the fact chunk is present and valid
 *    "strict"      - Like "truncate", but raise an error if the fact chunk
 *                    is invalid, not present for non-PCM formats, or if the
 *                    data chunk doesn't have that many samples
 *    "ignorezero"  - Like "truncate", but ignore fact chunk if the number of
 *                    samples is zero
 *    "ignore"      - Ignore fact chunk entirely (default)
 */
#define SDL_HINT_WAVE_FACT_CHUNK   "SDL_WAVE_FACT_CHUNK"

/**
 *  \brief  Controls how the size of the RIFF chunk affects the loading of a WAVE file.
 *
 *  The size of the RIFF chunk (which includes all the sub-chunks of the WAVE
 *  file) is not always reliable. In case the size is wrong, it's possible to
 *  just ignore it and step through the chunks until a fixed limit is reached.
 *
 *  Note that files that have trailing data unrelated to the WAVE file or
 *  corrupt files may slow down the loading process without a reliable boundary.
 *  By default, SDL stops after 10000 chunks to prevent wasting time. Use the
 *  environment variable SDL_WAVE_CHUNK_LIMIT to adjust this value.
 *
 *  This variable can be set to the following values:
 *
 *    "force"        - Always use the RIFF chunk size as a boundary for the chunk search
 *    "ignorezero"   - Like "force", but a zero size searches up to 4 GiB (default)
 *    "ignore"       - Ignore the RIFF chunk size and always search up to 4 GiB
 *    "maximum"      - Search for chunks until the end of file (not recommended)
 */
#define SDL_HINT_WAVE_RIFF_CHUNK_SIZE   "SDL_WAVE_RIFF_CHUNK_SIZE"

/**
 *  \brief  Controls how a truncated WAVE file is handled.
 *
 *  A WAVE file is considered truncated if any of the chunks are incomplete or
 *  the data chunk size is not a multiple of the block size. By default, SDL
 *  decodes until the first incomplete block, as most applications seem to do.
 *
 *  This variable can be set to the following values:
 *
 *    "verystrict" - Raise an error if the file is truncated
 *    "strict"     - Like "verystrict", but the size of the RIFF chunk is ignored
 *    "dropframe"  - Decode until the first incomplete sample frame
 *    "dropblock"  - Decode until the first incomplete block (default)
 */
#define SDL_HINT_WAVE_TRUNCATION   "SDL_WAVE_TRUNCATION"

/**
 * \brief Tell SDL not to name threads on Windows with the 0x406D1388 Exception.
 *        The 0x406D1388 Exception is a trick used to inform Visual Studio of a
 *        thread's name, but it tends to cause problems with other debuggers,
 *        and the .NET runtime. Note that SDL 2.0.6 and later will still use
 *        the (safer) SetThreadDescription API, introduced in the Windows 10
 *        Creators Update, if available.
 *
 * The variable can be set to the following values:
 *   "0"       - SDL will raise the 0x406D1388 Exception to name threads.
 *               This is the default behavior of SDL <= 2.0.4.
 *   "1"       - SDL will not raise this exception, and threads will be unnamed. (default)
 *               This is necessary with .NET languages or debuggers that aren't Visual Studio.
 */
#define SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING "SDL_WINDOWS_DISABLE_THREAD_NAMING"

/**
 *  \brief  A variable controlling whether the windows message loop is processed by SDL 
 *
 *  This variable can be set to the following values:
 *    "0"       - The window message loop is not run
 *    "1"       - The window message loop is processed in SDL_PumpEvents()
 *
 *  By default SDL will process the windows message loop
 */
#define SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP "SDL_WINDOWS_ENABLE_MESSAGELOOP"

/**
 * \brief Force SDL to use Critical Sections for mutexes on Windows.
 *        On Windows 7 and newer, Slim Reader/Writer Locks are available.
 *        They offer better performance, allocate no kernel ressources and
 *        use less memory. SDL will fall back to Critical Sections on older
 *        OS versions or if forced to by this hint.
 *
 *  This variable can be set to the following values:
 *    "0"       - Use SRW Locks when available. If not, fall back to Critical Sections. (default)
 *    "1"       - Force the use of Critical Sections in all cases.
 *
 */
#define SDL_HINT_WINDOWS_FORCE_MUTEX_CRITICAL_SECTIONS "SDL_WINDOWS_FORCE_MUTEX_CRITICAL_SECTIONS"

/**
 * \brief Force SDL to use Kernel Semaphores on Windows.
 *        Kernel Semaphores are inter-process and require a context
 *        switch on every interaction. On Windows 8 and newer, the
 *        WaitOnAddress API is available. Using that and atomics to
 *        implement semaphores increases performance.
 *        SDL will fall back to Kernel Objects on older OS versions
 *        or if forced to by this hint.
 *
 *  This variable can be set to the following values:
 *    "0"       - Use Atomics and WaitOnAddress API when available. If not, fall back to Kernel Objects. (default)
 *    "1"       - Force the use of Kernel Objects in all cases.
 *
 */
#define SDL_HINT_WINDOWS_FORCE_SEMAPHORE_KERNEL "SDL_WINDOWS_FORCE_SEMAPHORE_KERNEL"

/**
 * \brief A variable to specify custom icon resource id from RC file on Windows platform 
 */
#define SDL_HINT_WINDOWS_INTRESOURCE_ICON       "SDL_WINDOWS_INTRESOURCE_ICON"
#define SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL "SDL_WINDOWS_INTRESOURCE_ICON_SMALL"

/**
 *  \brief Tell SDL not to generate window-close events for Alt+F4 on Windows.
 *
 * The variable can be set to the following values:
 *   "0"       - SDL will generate a window-close event when it sees Alt+F4.
 *   "1"       - SDL will only do normal key handling for Alt+F4.
 */
#define SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4 "SDL_WINDOWS_NO_CLOSE_ON_ALT_F4"

/**
 * \brief Use the D3D9Ex API introduced in Windows Vista, instead of normal D3D9.
 *        Direct3D 9Ex contains changes to state management that can eliminate device
 *        loss errors during scenarios like Alt+Tab or UAC prompts. D3D9Ex may require
 *        some changes to your application to cope with the new behavior, so this
 *        is disabled by default.
 *
 *  This hint must be set before initializing the video subsystem.
 *
 *  For more information on Direct3D 9Ex, see:
 *    - https://docs.microsoft.com/en-us/windows/win32/direct3darticles/graphics-apis-in-windows-vista#direct3d-9ex
 *    - https://docs.microsoft.com/en-us/windows/win32/direct3darticles/direct3d-9ex-improvements
 *
 *  This variable can be set to the following values:
 *    "0"       - Use the original Direct3D 9 API (default)
 *    "1"       - Use the Direct3D 9Ex API on Vista and later (and fall back if D3D9Ex is unavailable)
 *
 */
#define SDL_HINT_WINDOWS_USE_D3D9EX "SDL_WINDOWS_USE_D3D9EX"

/**
 *  \brief  A variable controlling whether the window frame and title bar are interactive when the cursor is hidden 
 *
 *  This variable can be set to the following values:
 *    "0"       - The window frame is not interactive when the cursor is hidden (no move, resize, etc)
 *    "1"       - The window frame is interactive when the cursor is hidden
 *
 *  By default SDL will allow interaction with the window frame when the cursor is hidden
 */
#define SDL_HINT_WINDOW_FRAME_USABLE_WHILE_CURSOR_HIDDEN    "SDL_WINDOW_FRAME_USABLE_WHILE_CURSOR_HIDDEN"

/**
*  \brief  A variable controlling whether the window is activated when the SDL_ShowWindow function is called 
*
*  This variable can be set to the following values:
*    "0"       - The window is activated when the SDL_ShowWindow function is called
*    "1"       - The window is not activated when the SDL_ShowWindow function is called
*
*  By default SDL will activate the window when the SDL_ShowWindow function is called
*/
#define SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN    "SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN"

/** \brief Allows back-button-press events on Windows Phone to be marked as handled
 *
 *  Windows Phone devices typically feature a Back button.  When pressed,
 *  the OS will emit back-button-press events, which apps are expected to
 *  handle in an appropriate manner.  If apps do not explicitly mark these
 *  events as 'Handled', then the OS will invoke its default behavior for
 *  unhandled back-button-press events, which on Windows Phone 8 and 8.1 is to
 *  terminate the app (and attempt to switch to the previous app, or to the
 *  device's home screen).
 *
 *  Setting the SDL_HINT_WINRT_HANDLE_BACK_BUTTON hint to "1" will cause SDL
 *  to mark back-button-press events as Handled, if and when one is sent to
 *  the app.
 *
 *  Internally, Windows Phone sends back button events as parameters to
 *  special back-button-press callback functions.  Apps that need to respond
 *  to back-button-press events are expected to register one or more
 *  callback functions for such, shortly after being launched (during the
 *  app's initialization phase).  After the back button is pressed, the OS
 *  will invoke these callbacks.  If the app's callback(s) do not explicitly
 *  mark the event as handled by the time they return, or if the app never
 *  registers one of these callback, the OS will consider the event
 *  un-handled, and it will apply its default back button behavior (terminate
 *  the app).
 *
 *  SDL registers its own back-button-press callback with the Windows Phone
 *  OS.  This callback will emit a pair of SDL key-press events (SDL_KEYDOWN
 *  and SDL_KEYUP), each with a scancode of SDL_SCANCODE_AC_BACK, after which
 *  it will check the contents of the hint, SDL_HINT_WINRT_HANDLE_BACK_BUTTON.
 *  If the hint's value is set to "1", the back button event's Handled
 *  property will get set to 'true'.  If the hint's value is set to something
 *  else, or if it is unset, SDL will leave the event's Handled property
 *  alone.  (By default, the OS sets this property to 'false', to note.)
 *
 *  SDL apps can either set SDL_HINT_WINRT_HANDLE_BACK_BUTTON well before a
 *  back button is pressed, or can set it in direct-response to a back button
 *  being pressed.
 *
 *  In order to get notified when a back button is pressed, SDL apps should
 *  register a callback function with SDL_AddEventWatch(), and have it listen
 *  for SDL_KEYDOWN events that have a scancode of SDL_SCANCODE_AC_BACK.
 *  (Alternatively, SDL_KEYUP events can be listened-for.  Listening for
 *  either event type is suitable.)  Any value of SDL_HINT_WINRT_HANDLE_BACK_BUTTON
 *  set by such a callback, will be applied to the OS' current
 *  back-button-press event.
 *
 *  More details on back button behavior in Windows Phone apps can be found
 *  at the following page, on Microsoft's developer site:
 *  http://msdn.microsoft.com/en-us/library/windowsphone/develop/jj247550(v=vs.105).aspx
 */
#define SDL_HINT_WINRT_HANDLE_BACK_BUTTON "SDL_WINRT_HANDLE_BACK_BUTTON"

/** \brief Label text for a WinRT app's privacy policy link
 *
 *  Network-enabled WinRT apps must include a privacy policy.  On Windows 8, 8.1, and RT,
 *  Microsoft mandates that this policy be available via the Windows Settings charm.
 *  SDL provides code to add a link there, with its label text being set via the
 *  optional hint, SDL_HINT_WINRT_PRIVACY_POLICY_LABEL.
 *
 *  Please note that a privacy policy's contents are not set via this hint.  A separate
 *  hint, SDL_HINT_WINRT_PRIVACY_POLICY_URL, is used to link to the actual text of the
 *  policy.
 *
 *  The contents of this hint should be encoded as a UTF8 string.
 *
 *  The default value is "Privacy Policy".  This hint should only be set during app
 *  initialization, preferably before any calls to SDL_Init().
 *
 *  For additional information on linking to a privacy policy, see the documentation for
 *  SDL_HINT_WINRT_PRIVACY_POLICY_URL.
 */
#define SDL_HINT_WINRT_PRIVACY_POLICY_LABEL "SDL_WINRT_PRIVACY_POLICY_LABEL"

/**
 *  \brief A URL to a WinRT app's privacy policy
 *
 *  All network-enabled WinRT apps must make a privacy policy available to its
 *  users.  On Windows 8, 8.1, and RT, Microsoft mandates that this policy be
 *  be available in the Windows Settings charm, as accessed from within the app.
 *  SDL provides code to add a URL-based link there, which can point to the app's
 *  privacy policy.
 *
 *  To setup a URL to an app's privacy policy, set SDL_HINT_WINRT_PRIVACY_POLICY_URL
 *  before calling any SDL_Init() functions.  The contents of the hint should
 *  be a valid URL.  For example, "http://www.example.com".
 *
 *  The default value is "", which will prevent SDL from adding a privacy policy
 *  link to the Settings charm.  This hint should only be set during app init.
 *
 *  The label text of an app's "Privacy Policy" link may be customized via another
 *  hint, SDL_HINT_WINRT_PRIVACY_POLICY_LABEL.
 *
 *  Please note that on Windows Phone, Microsoft does not provide standard UI
 *  for displaying a privacy policy link, and as such, SDL_HINT_WINRT_PRIVACY_POLICY_URL
 *  will not get used on that platform.  Network-enabled phone apps should display
 *  their privacy policy through some other, in-app means.
 */
#define SDL_HINT_WINRT_PRIVACY_POLICY_URL "SDL_WINRT_PRIVACY_POLICY_URL"

/**
 *  \brief Mark X11 windows as override-redirect.
 *
 *  If set, this _might_ increase framerate at the expense of the desktop
 *  not working as expected. Override-redirect windows aren't noticed by the
 *  window manager at all.
 *
 *  You should probably only use this for fullscreen windows, and you probably
 *  shouldn't even use it for that. But it's here if you want to try!
 */
#define SDL_HINT_X11_FORCE_OVERRIDE_REDIRECT "SDL_X11_FORCE_OVERRIDE_REDIRECT"

/**
 *  \brief  A variable that lets you disable the detection and use of Xinput gamepad devices
 *
 *  The variable can be set to the following values:
 *    "0"       - Disable XInput detection (only uses direct input)
 *    "1"       - Enable XInput detection (the default)
 */
#define SDL_HINT_XINPUT_ENABLED "SDL_XINPUT_ENABLED"

/**
 *  \brief  A variable that causes SDL to use the old axis and button mapping for XInput devices.
 *
 *  This hint is for backwards compatibility only and will be removed in SDL 2.1
 *
 *  The default value is "0".  This hint must be set before SDL_Init()
 */
#define SDL_HINT_XINPUT_USE_OLD_JOYSTICK_MAPPING "SDL_XINPUT_USE_OLD_JOYSTICK_MAPPING"

/**
 *  \brief  A variable that causes SDL to not ignore audio "monitors"
 *
 *  This is currently only used for PulseAudio and ignored elsewhere.
 *
 *  By default, SDL ignores audio devices that aren't associated with physical
 *  hardware. Changing this hint to "1" will expose anything SDL sees that
 *  appears to be an audio source or sink. This will add "devices" to the list
 *  that the user probably doesn't want or need, but it can be useful in
 *  scenarios where you want to hook up SDL to some sort of virtual device,
 *  etc.
 *
 *  The default value is "0".  This hint must be set before SDL_Init().
 *
 *  This hint is available since SDL 2.0.16. Before then, virtual devices are
 *  always ignored.
 */
#define SDL_HINT_AUDIO_INCLUDE_MONITORS "SDL_AUDIO_INCLUDE_MONITORS"


/**
 *  \brief  An enumeration of hint priorities
 */
typedef enum
{
    SDL_HINT_DEFAULT,
    SDL_HINT_NORMAL,
    SDL_HINT_OVERRIDE
} SDL_HintPriority;


/**
 * Set a hint with a specific priority.
 *
 * The priority controls the behavior when setting a hint that already has a
 * value. Hints will replace existing hints of their priority and lower.
 * Environment variables are considered to have override priority.
 *
 * \param name the hint to set
 * \param value the value of the hint variable
 * \param priority the SDL_HintPriority level for the hint
 * \returns SDL_TRUE if the hint was set, SDL_FALSE otherwise.
 *
 * \since This function is available since SDL 2.0.0.
 *
 * \sa SDL_GetHint
 * \sa SDL_SetHint
 */
extern DECLSPEC SDL_bool SDLCALL SDL_SetHintWithPriority(const char *name,
                                                         const char *value,
                                                         SDL_HintPriority priority);

/**
 * Set a hint with normal priority.
 *
 * Hints will not be set if there is an existing override hint or environment
 * variable that takes precedence. You can use SDL_SetHintWithPriority() to
 * set the hint with override priority instead.
 *
 * \param name the hint to set
 * \param value the value of the hint variable
 * \returns SDL_TRUE if the hint was set, SDL_FALSE otherwise.
 *
 * \since This function is available since SDL 2.0.0.
 *
 * \sa SDL_GetHint
 * \sa SDL_SetHintWithPriority
 */
extern DECLSPEC SDL_bool SDLCALL SDL_SetHint(const char *name,
                                             const char *value);

/**
 * Get the value of a hint.
 *
 * \param name the hint to query
 * \returns the string value of a hint or NULL if the hint isn't set.
 *
 * \since This function is available since SDL 2.0.0.
 *
 * \sa SDL_SetHint
 * \sa SDL_SetHintWithPriority
 */
extern DECLSPEC const char * SDLCALL SDL_GetHint(const char *name);

/**
 * Get the boolean value of a hint variable.
 *
 * \param name the name of the hint to get the boolean value from
 * \param default_value the value to return if the hint does not exist
 * \returns the boolean value of a hint or the provided default value if the
 *          hint does not exist.
 *
 * \since This function is available since SDL 2.0.5.
 *
 * \sa SDL_GetHint
 * \sa SDL_SetHint
 */
extern DECLSPEC SDL_bool SDLCALL SDL_GetHintBoolean(const char *name, SDL_bool default_value);

/**
 * Type definition of the hint callback function.
 *
 * \param userdata what was passed as `userdata` to SDL_AddHintCallback()
 * \param name what was passed as `name` to SDL_AddHintCallback()
 * \param oldValue the previous hint value
 * \param newValue the new value hint is to be set to
 */
typedef void (SDLCALL *SDL_HintCallback)(void *userdata, const char *name, const char *oldValue, const char *newValue);

/**
 * Add a function to watch a particular hint.
 *
 * \param name the hint to watch
 * \param callback An SDL_HintCallback function that will be called when the
 *                 hint value changes
 * \param userdata a pointer to pass to the callback function
 *
 * \since This function is available since SDL 2.0.0.
 *
 * \sa SDL_DelHintCallback
 */
extern DECLSPEC void SDLCALL SDL_AddHintCallback(const char *name,
                                                 SDL_HintCallback callback,
                                                 void *userdata);

/**
 * Remove a function watching a particular hint.
 *
 * \param name the hint being watched
 * \param callback An SDL_HintCallback function that will be called when the
 *                 hint value changes
 * \param userdata a pointer being passed to the callback function
 *
 * \since This function is available since SDL 2.0.0.
 *
 * \sa SDL_AddHintCallback
 */
extern DECLSPEC void SDLCALL SDL_DelHintCallback(const char *name,
                                                 SDL_HintCallback callback,
                                                 void *userdata);

/**
 * Clear all hints.
 *
 * This function is automatically called during SDL_Quit().
 *
 * \since This function is available since SDL 2.0.0.
 */
extern DECLSPEC void SDLCALL SDL_ClearHints(void);


/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include "close_code.h"

#endif /* SDL_hints_h_ */

/* vi: set ts=4 sw=4 expandtab: */
