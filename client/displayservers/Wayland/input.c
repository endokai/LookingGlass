/**
 * Looking Glass
 * Copyright (C) 2017-2021 The Looking Glass Authors
 * https://looking-glass.io
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "wayland.h"

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>

#include "app.h"
#include "common/debug.h"

// Mouse-handling listeners.

static void pointerMotionHandler(void * data, struct wl_pointer * pointer,
    uint32_t serial, wl_fixed_t sxW, wl_fixed_t syW)
{
  wlWm.cursorX = wl_fixed_to_double(sxW);
  wlWm.cursorY = wl_fixed_to_double(syW);
  app_updateCursorPos(wlWm.cursorX, wlWm.cursorY);

  if (!wlWm.warpSupport && !wlWm.relativePointer)
    app_handleMouseBasic();
}

static void pointerEnterHandler(void * data, struct wl_pointer * pointer,
    uint32_t serial, struct wl_surface * surface, wl_fixed_t sxW,
    wl_fixed_t syW)
{
  if (surface != wlWm.surface)
    return;

  wlWm.pointerInSurface = true;
  app_handleEnterEvent(true);

  wl_pointer_set_cursor(pointer, serial, wlWm.showPointer ? wlWm.cursor : NULL, 0, 0);
  wlWm.pointerEnterSerial = serial;

  wlWm.cursorX = wl_fixed_to_double(sxW);
  wlWm.cursorY = wl_fixed_to_double(syW);
  app_updateCursorPos(wlWm.cursorX, wlWm.cursorY);

  if (wlWm.warpSupport)
  {
    app_handleMouseRelative(0.0, 0.0, 0.0, 0.0);
    return;
  }

  if (wlWm.relativePointer)
    return;

  app_resyncMouseBasic();
  app_handleMouseBasic();
}

static void pointerLeaveHandler(void * data, struct wl_pointer * pointer,
    uint32_t serial, struct wl_surface * surface)
{
  if (surface != wlWm.surface)
    return;

  wlWm.pointerInSurface = false;
  app_handleEnterEvent(false);
}

static void pointerAxisHandler(void * data, struct wl_pointer * pointer,
  uint32_t serial, uint32_t axis, wl_fixed_t value)
{
  int button = value > 0 ?
    5 /* SPICE_MOUSE_BUTTON_DOWN */ :
    4 /* SPICE_MOUSE_BUTTON_UP */;
  app_handleButtonPress(button);
  app_handleButtonRelease(button);
}

static int mapWaylandToSpiceButton(uint32_t button)
{
  switch (button)
  {
    case BTN_LEFT:
      return 1;  // SPICE_MOUSE_BUTTON_LEFT
    case BTN_MIDDLE:
      return 2;  // SPICE_MOUSE_BUTTON_MIDDLE
    case BTN_RIGHT:
      return 3;  // SPICE_MOUSE_BUTTON_RIGHT
    case BTN_SIDE:
      return 6;  // SPICE_MOUSE_BUTTON_SIDE
    case BTN_EXTRA:
      return 7;  // SPICE_MOUSE_BUTTON_EXTRA
  }

  return 0;  // SPICE_MOUSE_BUTTON_INVALID
}

static void pointerButtonHandler(void *data, struct wl_pointer *pointer,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t stateW)
{
  button = mapWaylandToSpiceButton(button);

  if (stateW == WL_POINTER_BUTTON_STATE_PRESSED)
    app_handleButtonPress(button);
  else
    app_handleButtonRelease(button);
}

static const struct wl_pointer_listener pointerListener = {
  .enter = pointerEnterHandler,
  .leave = pointerLeaveHandler,
  .motion = pointerMotionHandler,
  .button = pointerButtonHandler,
  .axis = pointerAxisHandler,
};

static void relativePointerMotionHandler(void * data,
    struct zwp_relative_pointer_v1 *pointer, uint32_t timeHi, uint32_t timeLo,
    wl_fixed_t dxW, wl_fixed_t dyW, wl_fixed_t dxUnaccelW,
    wl_fixed_t dyUnaccelW)
{
  wlWm.cursorX += wl_fixed_to_double(dxW);
  wlWm.cursorY += wl_fixed_to_double(dyW);
  app_updateCursorPos(wlWm.cursorX, wlWm.cursorY);

  app_handleMouseRelative(
      wl_fixed_to_double(dxW),
      wl_fixed_to_double(dyW),
      wl_fixed_to_double(dxUnaccelW),
      wl_fixed_to_double(dyUnaccelW));
}

static const struct zwp_relative_pointer_v1_listener relativePointerListener = {
    .relative_motion = relativePointerMotionHandler,
};

// Keyboard-handling listeners.

static void keyboardKeymapHandler(void * data, struct wl_keyboard * keyboard,
    uint32_t format, int fd, uint32_t size)
{
  close(fd);
}

static void keyboardEnterHandler(void * data, struct wl_keyboard * keyboard,
    uint32_t serial, struct wl_surface * surface, struct wl_array * keys)
{
  if (surface != wlWm.surface)
    return;

  wlWm.focusedOnSurface = true;
  app_handleFocusEvent(true);
  wlWm.keyboardEnterSerial = serial;

  uint32_t * key;
  wl_array_for_each(key, keys)
    app_handleKeyPress(*key);
}

static void keyboardLeaveHandler(void * data, struct wl_keyboard * keyboard,
    uint32_t serial, struct wl_surface * surface)
{
  if (surface != wlWm.surface)
    return;

  wlWm.focusedOnSurface = false;
  waylandCBInvalidate();
  app_handleFocusEvent(false);
}

static void keyboardKeyHandler(void * data, struct wl_keyboard * keyboard,
    uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
  if (!wlWm.focusedOnSurface)
    return;

  if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
    app_handleKeyPress(key);
  else
    app_handleKeyRelease(key);
}

static void keyboardModifiersHandler(void * data,
    struct wl_keyboard * keyboard, uint32_t serial, uint32_t modsDepressed,
    uint32_t modsLatched, uint32_t modsLocked, uint32_t group)
{
  // Do nothing.
}

static const struct wl_keyboard_listener keyboardListener = {
  .keymap = keyboardKeymapHandler,
  .enter = keyboardEnterHandler,
  .leave = keyboardLeaveHandler,
  .key = keyboardKeyHandler,
  .modifiers = keyboardModifiersHandler,
};

// Seat-handling listeners.

static void handlePointerCapability(uint32_t capabilities)
{
  bool hasPointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
  if (!hasPointer && wlWm.pointer)
  {
    wl_pointer_destroy(wlWm.pointer);
    wlWm.pointer = NULL;
  }
  else if (hasPointer && !wlWm.pointer)
  {
    wlWm.pointer = wl_seat_get_pointer(wlWm.seat);
    wl_pointer_add_listener(wlWm.pointer, &pointerListener, NULL);
  }
}

static void handleKeyboardCapability(uint32_t capabilities)
{
  bool hasKeyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
  if (!hasKeyboard && wlWm.keyboard)
  {
    wl_keyboard_destroy(wlWm.keyboard);
    wlWm.keyboard = NULL;
  }
  else if (hasKeyboard && !wlWm.keyboard)
  {
    wlWm.keyboard = wl_seat_get_keyboard(wlWm.seat);
    wl_keyboard_add_listener(wlWm.keyboard, &keyboardListener, NULL);
  }
}

static void seatCapabilitiesHandler(void * data, struct wl_seat * seat,
    uint32_t capabilities)
{
  wlWm.capabilities = capabilities;
  handlePointerCapability(capabilities);
  handleKeyboardCapability(capabilities);
}

static void seatNameHandler(void * data, struct wl_seat * seat,
    const char * name)
{
  // Do nothing.
}

static const struct wl_seat_listener seatListener = {
    .capabilities = seatCapabilitiesHandler,
    .name = seatNameHandler,
};

bool waylandInputInit(void)
{
  if (!wlWm.seat)
  {
    DEBUG_ERROR("Compositor missing wl_seat, will not proceed");
    return false;
  }

  if (wlWm.warpSupport && (!wlWm.relativePointerManager || !wlWm.pointerConstraints))
  {
    DEBUG_WARN("Cursor warp is requested, but cannot be honoured due to lack "
               "of zwp_relative_pointer_manager_v1 or zwp_pointer_constraints_v1");
    wlWm.warpSupport = false;
  }

  if (!wlWm.relativePointerManager)
    DEBUG_WARN("zwp_relative_pointer_manager_v1 not exported by compositor, "
               "mouse will not be captured");

  if (!wlWm.pointerConstraints)
    DEBUG_WARN("zwp_pointer_constraints_v1 not exported by compositor, mouse "
               "will not be captured");

  if (!wlWm.keyboardInhibitManager)
    DEBUG_WARN("zwp_keyboard_shortcuts_inhibit_manager_v1 not exported by "
               "compositor, keyboard will not be grabbed");

  wl_seat_add_listener(wlWm.seat, &seatListener, NULL);
  wl_display_roundtrip(wlWm.display);

  if (wlWm.warpSupport)
  {
    wlWm.relativePointer =
      zwp_relative_pointer_manager_v1_get_relative_pointer(
        wlWm.relativePointerManager, wlWm.pointer);
    zwp_relative_pointer_v1_add_listener(wlWm.relativePointer,
      &relativePointerListener, NULL);
  }

  LG_LOCK_INIT(wlWm.confineLock);

  return true;
}

void waylandInputFree(void)
{
  waylandUngrabPointer();
  LG_LOCK_FREE(wlWm.confineLock);
  wl_pointer_destroy(wlWm.pointer);
  wl_keyboard_destroy(wlWm.keyboard);
  wl_seat_destroy(wlWm.seat);
}

void waylandGrabPointer(void)
{
  if (!wlWm.relativePointerManager || !wlWm.pointerConstraints)
    return;

  if (!wlWm.warpSupport && !wlWm.relativePointer)
  {
    wlWm.relativePointer =
      zwp_relative_pointer_manager_v1_get_relative_pointer(
        wlWm.relativePointerManager, wlWm.pointer);
    zwp_relative_pointer_v1_add_listener(wlWm.relativePointer,
      &relativePointerListener, NULL);
  }

  INTERLOCKED_SECTION(wlWm.confineLock,
  {
    if (!wlWm.confinedPointer)
    {
      wlWm.confinedPointer = zwp_pointer_constraints_v1_confine_pointer(
          wlWm.pointerConstraints, wlWm.surface, wlWm.pointer, NULL,
          ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    }
  });
}

void waylandUngrabPointer(void)
{
  INTERLOCKED_SECTION(wlWm.confineLock,
  {
    if (wlWm.confinedPointer)
    {
      zwp_confined_pointer_v1_destroy(wlWm.confinedPointer);
      wlWm.confinedPointer = NULL;
    }
  });

  if (!wlWm.warpSupport)
  {
    if (!wlWm.relativePointer)
    {
      wlWm.relativePointer =
        zwp_relative_pointer_manager_v1_get_relative_pointer(
          wlWm.relativePointerManager, wlWm.pointer);
      zwp_relative_pointer_v1_add_listener(wlWm.relativePointer,
        &relativePointerListener, NULL);
    }

    app_resyncMouseBasic();
    app_handleMouseBasic();
  }
}

void waylandCapturePointer(void)
{
  if (!wlWm.warpSupport)
  {
    waylandGrabPointer();
    return;
  }

  INTERLOCKED_SECTION(wlWm.confineLock,
  {
    if (wlWm.confinedPointer)
    {
      zwp_confined_pointer_v1_destroy(wlWm.confinedPointer);
      wlWm.confinedPointer = NULL;
    }

    wlWm.lockedPointer = zwp_pointer_constraints_v1_lock_pointer(
          wlWm.pointerConstraints, wlWm.surface, wlWm.pointer, NULL,
          ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
  });
}

void waylandUncapturePointer(void)
{
  INTERLOCKED_SECTION(wlWm.confineLock,
  {
    if (wlWm.lockedPointer)
    {
      zwp_locked_pointer_v1_destroy(wlWm.lockedPointer);
      wlWm.lockedPointer = NULL;
    }

    /* we need to ungrab the pointer on the following conditions when exiting capture mode:
     *   - if warp is not supported, exit via window edge detection will never work
     *     as the cursor can not be warped out of the window when we release it.
     *   - if the format is invalid as we do not know where the guest cursor is,
     *     which also breaks edge detection.
     *   - if the user has opted to use captureInputOnly mode.
     */
    if (!wlWm.warpSupport || !app_isFormatValid() || app_isCaptureOnlyMode())
    {
      waylandUngrabPointer();
    }
    else
    {
      wlWm.confinedPointer = zwp_pointer_constraints_v1_confine_pointer(
          wlWm.pointerConstraints, wlWm.surface, wlWm.pointer, NULL,
          ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    }
  });
}

void waylandGrabKeyboard(void)
{
  if (wlWm.keyboardInhibitManager && !wlWm.keyboardInhibitor)
  {
    wlWm.keyboardInhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
        wlWm.keyboardInhibitManager, wlWm.surface, wlWm.seat);
  }
}

void waylandUngrabKeyboard(void)
{
  if (wlWm.keyboardInhibitor)
  {
    zwp_keyboard_shortcuts_inhibitor_v1_destroy(wlWm.keyboardInhibitor);
    wlWm.keyboardInhibitor = NULL;
  }
}

void waylandWarpPointer(int x, int y, bool exiting)
{
  if (!wlWm.pointerInSurface)
    return;

  INTERLOCKED_SECTION(wlWm.confineLock,
  {
    if (!wlWm.lockedPointer)
    {
      LG_UNLOCK(wlWm.confineLock);
      return;
    }

    if (x < 0) x = 0;
    else if (x >= wlWm.width) x = wlWm.width - 1;
    if (y < 0) y = 0;
    else if (y >= wlWm.height) y = wlWm.height - 1;

    struct wl_region * region = wl_compositor_create_region(wlWm.compositor);
    wl_region_add(region, x, y, 1, 1);

    if (wlWm.confinedPointer)
    {
      zwp_confined_pointer_v1_set_region(wlWm.confinedPointer, region);
      wl_surface_commit(wlWm.surface);
      zwp_confined_pointer_v1_set_region(wlWm.confinedPointer, NULL);
    }
    else
    {
      struct zwp_confined_pointer_v1 * confine;
      confine = zwp_pointer_constraints_v1_confine_pointer(
            wlWm.pointerConstraints, wlWm.surface, wlWm.pointer, region,
            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
      wl_surface_commit(wlWm.surface);
      zwp_confined_pointer_v1_destroy(confine);
    }

    wl_surface_commit(wlWm.surface);
    wl_region_destroy(region);
  });
}

void waylandRealignPointer(void)
{
  if (!wlWm.warpSupport)
    app_resyncMouseBasic();
}

void waylandGuestPointerUpdated(double x, double y, double localX, double localY)
{
  if (!wlWm.warpSupport || !wlWm.pointerInSurface || wlWm.lockedPointer)
    return;

  waylandWarpPointer((int) localX, (int) localY, false);
}
