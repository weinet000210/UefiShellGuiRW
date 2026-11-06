/** @file
  MDI child windows inside the desktop, in the style of RW-Everything:
  caption with icon and title, minimize / maximize / close buttons, drag by
  the caption, resize from the bottom-right grip, click to bring to front,
  cascade and tile.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"

#define MDI_MIN_WIDTH       160
#define MDI_MIN_HEIGHT      80
#define MDI_ICONIC_WIDTH    170
#define MDI_CASCADE_STEP    22
#define MDI_BUTTON_W        18
#define MDI_BUTTON_H        15

STATIC lv_obj_t      *mDesktop;
STATIC RW_MDI_WINDOW *mFirst;      // creation order
STATIC RW_MDI_WINDOW *mActive;
STATIC UINTN         mCreated;     // for the default cascade position

STATIC
VOID
MdiLayoutIconic (
  VOID
  );

STATIC
INT32
DesktopWidth (
  VOID
  )
{
  lv_obj_update_layout (mDesktop);
  return lv_obj_get_content_width (mDesktop);
}

STATIC
INT32
DesktopHeight (
  VOID
  )
{
  lv_obj_update_layout (mDesktop);
  return lv_obj_get_content_height (mDesktop);
}

STATIC
VOID
SetCaptionColors (
  IN RW_MDI_WINDOW  *Window,
  IN BOOLEAN        Active
  )
{
  lv_obj_set_style_bg_color (Window->Caption, lv_color_hex (Active ? RW_COLOR_TITLE_A1 : RW_COLOR_TITLE_I1), 0);
  lv_obj_set_style_bg_grad_color (Window->Caption, lv_color_hex (Active ? RW_COLOR_TITLE_A2 : RW_COLOR_TITLE_I2), 0);
  lv_obj_set_style_text_color (Window->Title, lv_color_hex (Active ? 0xFFFFFF : 0xD4D0C8), 0);
}

STATIC
VOID
SetGeometry (
  IN RW_MDI_WINDOW  *Window,
  IN INT32          X,
  IN INT32          Y,
  IN INT32          W,
  IN INT32          H
  )
{
  lv_obj_set_pos (Window->Obj, X, Y);
  lv_obj_set_size (Window->Obj, W, H);
}

STATIC
VOID
SaveRestore (
  IN RW_MDI_WINDOW  *Window
  )
{
  Window->Restore.x1 = lv_obj_get_x (Window->Obj);
  Window->Restore.y1 = lv_obj_get_y (Window->Obj);
  Window->Restore.x2 = Window->Restore.x1 + lv_obj_get_width (Window->Obj) - 1;
  Window->Restore.y2 = Window->Restore.y1 + lv_obj_get_height (Window->Obj) - 1;
}

STATIC
VOID
ApplyRestore (
  IN RW_MDI_WINDOW  *Window
  )
{
  SetGeometry (
    Window,
    Window->Restore.x1,
    Window->Restore.y1,
    lv_area_get_width (&Window->Restore),
    lv_area_get_height (&Window->Restore)
    );
}

//
// Maximize button glyph: one box (maximize) or two boxes (restore), drawn
// with plain objects because the built-in fonts have no such symbol.
//
STATIC
VOID
DrawMaxGlyph (
  IN RW_MDI_WINDOW  *Window
  )
{
  lv_obj_t  *Box;

  lv_obj_clean (Window->MaxButton);
  if (Window->Maximized || Window->Minimized) {
    Box = lv_obj_create (Window->MaxButton);
    lv_obj_remove_style_all (Box);
    lv_obj_set_size (Box, 7, 6);
    lv_obj_set_pos (Box, 7, 2);
    lv_obj_set_style_border_width (Box, 1, 0);
    lv_obj_set_style_border_color (Box, lv_color_black (), 0);
    lv_obj_set_clickable (Box, FALSE);
    Box = lv_obj_create (Window->MaxButton);
    lv_obj_remove_style_all (Box);
    lv_obj_set_size (Box, 7, 6);
    lv_obj_set_pos (Box, 4, 5);
    lv_obj_set_style_bg_color (Box, lv_color_hex (RW_COLOR_FACE), 0);
    lv_obj_set_style_bg_opa (Box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width (Box, 1, 0);
    lv_obj_set_style_border_color (Box, lv_color_black (), 0);
    lv_obj_set_clickable (Box, FALSE);
  } else {
    Box = lv_obj_create (Window->MaxButton);
    lv_obj_remove_style_all (Box);
    lv_obj_set_size (Box, 9, 8);
    lv_obj_set_pos (Box, 4, 3);
    lv_obj_set_style_border_width (Box, 1, 0);
    lv_obj_set_style_border_color (Box, lv_color_black (), 0);
    lv_obj_set_style_border_side (Box, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_outline_width (Box, 0, 0);
    lv_obj_set_clickable (Box, FALSE);
    //
    // Thick top edge like the Windows glyph.
    //
    Box = lv_obj_create (Window->MaxButton);
    lv_obj_remove_style_all (Box);
    lv_obj_set_size (Box, 9, 2);
    lv_obj_set_pos (Box, 4, 3);
    lv_obj_set_style_bg_color (Box, lv_color_black (), 0);
    lv_obj_set_style_bg_opa (Box, LV_OPA_COVER, 0);
    lv_obj_set_clickable (Box, FALSE);
  }
}

STATIC
lv_obj_t *
CaptionButton (
  IN lv_obj_t     *Caption,
  IN CONST CHAR8  *Symbol  OPTIONAL
  )
{
  lv_obj_t  *Button;
  lv_obj_t  *Label;

  Button = lv_obj_create (Caption);
  lv_obj_remove_style_all (Button);
  lv_obj_set_size (Button, MDI_BUTTON_W, MDI_BUTTON_H);
  lv_obj_set_style_bg_color (Button, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_color (Button, lv_color_hex (0xD8D8D8), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa (Button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Button, 1, 0);
  lv_obj_set_style_border_color (Button, lv_color_hex (RW_COLOR_DARK), 0);
  lv_obj_set_style_radius (Button, 2, 0);
  lv_obj_set_style_margin_left (Button, 2, 0);
  lv_obj_set_scrollable (Button, FALSE);
  lv_obj_set_clickable (Button, TRUE);
  if (Symbol != NULL) {
    Label = lv_label_create (Button);
    lv_label_set_text (Label, Symbol);
    lv_obj_set_style_text_color (Label, lv_color_black (), 0);
    lv_obj_set_style_text_font (Label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_scale (Label, 180, 0);
    lv_obj_center (Label);
  }

  return Button;
}

STATIC
VOID
OnWindowPressed (
  lv_event_t  *Event
  )
{
  RW_MDI_WINDOW  *Window;

  Window = (RW_MDI_WINDOW *)lv_event_get_user_data (Event);
  if (Window != mActive) {
    RwMdiActivate (Window);
  }
}

STATIC
VOID
OnCaptionDrag (
  lv_event_t  *Event
  )
{
  RW_MDI_WINDOW  *Window;
  lv_point_t     Vect;
  INT32          X;
  INT32          Y;
  INT32          W;
  INT32          H;

  Window = (RW_MDI_WINDOW *)lv_event_get_user_data (Event);
  if (Window->Maximized || Window->Minimized) {
    return;
  }

  lv_indev_get_vect (lv_indev_active (), &Vect);
  if ((Vect.x == 0) && (Vect.y == 0)) {
    return;
  }

  X = lv_obj_get_x (Window->Obj) + Vect.x;
  Y = lv_obj_get_y (Window->Obj) + Vect.y;
  W = DesktopWidth ();
  H = DesktopHeight ();
  //
  // Keep part of the caption inside the desktop so the window can always
  // be grabbed again.
  //
  X = MAX (X, 40 - lv_obj_get_width (Window->Obj));
  X = MIN (X, W - 40);
  Y = MAX (Y, 0);
  Y = MIN (Y, H - RW_CAPTION_HEIGHT);
  lv_obj_set_pos (Window->Obj, X, Y);
}

STATIC
VOID
OnCaptionDoubleClick (
  lv_event_t  *Event
  )
{
  RW_MDI_WINDOW  *Window;

  Window = (RW_MDI_WINDOW *)lv_event_get_user_data (Event);
  if (Window->Minimized) {
    RwMdiSetMinimized (Window, FALSE);
  } else {
    RwMdiSetMaximized (Window, (BOOLEAN)!Window->Maximized);
  }
}

STATIC
VOID
OnGripDrag (
  lv_event_t  *Event
  )
{
  RW_MDI_WINDOW  *Window;
  lv_point_t     Vect;
  INT32          W;
  INT32          H;

  Window = (RW_MDI_WINDOW *)lv_event_get_user_data (Event);
  if (Window->Maximized || Window->Minimized) {
    return;
  }

  lv_indev_get_vect (lv_indev_active (), &Vect);
  W = MAX (MDI_MIN_WIDTH, lv_obj_get_width (Window->Obj) + Vect.x);
  H = MAX (MDI_MIN_HEIGHT, lv_obj_get_height (Window->Obj) + Vect.y);
  lv_obj_set_size (Window->Obj, W, H);
}

STATIC
VOID
OnMinimize (
  lv_event_t  *Event
  )
{
  RW_MDI_WINDOW  *Window;

  Window = (RW_MDI_WINDOW *)lv_event_get_user_data (Event);
  RwMdiSetMinimized (Window, (BOOLEAN)!Window->Minimized);
}

STATIC
VOID
OnMaximize (
  lv_event_t  *Event
  )
{
  RW_MDI_WINDOW  *Window;

  Window = (RW_MDI_WINDOW *)lv_event_get_user_data (Event);
  if (Window->Minimized) {
    RwMdiSetMinimized (Window, FALSE);
  } else {
    RwMdiSetMaximized (Window, (BOOLEAN)!Window->Maximized);
  }
}

STATIC
VOID
OnClose (
  lv_event_t  *Event
  )
{
  RwMdiClose ((RW_MDI_WINDOW *)lv_event_get_user_data (Event));
}

VOID
RwMdiInitialize (
  IN lv_obj_t  *Desktop
  )
{
  mDesktop = Desktop;
  mFirst   = NULL;
  mActive  = NULL;
  mCreated = 0;
}

RW_MDI_WINDOW *
RwMdiCreate (
  IN CONST CHAR8     *Title,
  IN RW_COMMAND_ID   Command,
  IN INT32           Width,
  IN INT32           Height
  )
{
  RW_MDI_WINDOW     *Window;
  RW_MDI_WINDOW     **Link;
  CONST RW_COMMAND  *Cmd;
  lv_obj_t          *Obj;
  lv_obj_t          *Icon;
  lv_obj_t          *Button;
  lv_obj_t          *Grip;
  lv_obj_t          *Line;
  INT32             Offset;
  INT32             W;
  INT32             H;
  STATIC lv_point_precise_t  GripPoints[2][2] = {
    { { 11, 3 }, { 3, 11 } },
    { { 11, 7 }, { 7, 11 } }
  };

  Window = AllocateZeroPool (sizeof (*Window));
  if (Window == NULL) {
    return NULL;
  }

  Window->Command = Command;
  Cmd             = RwGetCommand (Command);

  //
  // Frame: raised 3D border around the caption and client area.
  //
  Obj = lv_obj_create (mDesktop);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Obj, 1, 0);
  lv_obj_set_style_border_color (Obj, lv_color_hex (RW_COLOR_DARK), 0);
  lv_obj_set_style_pad_all (Obj, 3, 0);
  lv_obj_set_style_pad_row (Obj, 2, 0);
  lv_obj_set_style_shadow_width (Obj, 6, 0);
  lv_obj_set_style_shadow_offset_x (Obj, 2, 0);
  lv_obj_set_style_shadow_offset_y (Obj, 2, 0);
  lv_obj_set_style_shadow_opa (Obj, LV_OPA_30, 0);
  lv_obj_set_flex_flow (Obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_set_clickable (Obj, TRUE);
  lv_obj_add_event_cb (Obj, OnWindowPressed, LV_EVENT_PRESSED, Window);
  Window->Obj = Obj;

  //
  // Caption: gradient bar, icon, title, buttons.
  //
  Window->Caption = lv_obj_create (Obj);
  lv_obj_remove_style_all (Window->Caption);
  lv_obj_set_size (Window->Caption, LV_PCT (100), RW_CAPTION_HEIGHT);
  lv_obj_set_style_bg_opa (Window->Caption, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir (Window->Caption, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_pad_left (Window->Caption, 2, 0);
  lv_obj_set_style_pad_right (Window->Caption, 2, 0);
  lv_obj_set_style_pad_column (Window->Caption, 3, 0);
  lv_obj_set_flex_flow (Window->Caption, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (Window->Caption, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable (Window->Caption, FALSE);
  lv_obj_set_clickable (Window->Caption, TRUE);
  lv_obj_set_event_bubble (Window->Caption, TRUE);
  lv_obj_add_event_cb (Window->Caption, OnCaptionDrag, LV_EVENT_PRESSING, Window);
  lv_obj_add_event_cb (Window->Caption, OnCaptionDoubleClick, LV_EVENT_DOUBLE_CLICKED, Window);

  if ((Cmd != NULL) && (Cmd->Icon != NULL)) {
    Icon = lv_image_create (Window->Caption);
    lv_image_set_src (Icon, Cmd->Icon);
    lv_image_set_inner_align (Icon, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_size (Icon, 16, 16);
    lv_obj_set_clickable (Icon, FALSE);
  }

  Window->Title = lv_label_create (Window->Caption);
  lv_label_set_text (Window->Title, Title);
  lv_label_set_long_mode (Window->Title, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_flex_grow (Window->Title, 1);
  lv_obj_set_style_text_font (Window->Title, &lv_font_montserrat_14, 0);

  Button = CaptionButton (Window->Caption, LV_SYMBOL_MINUS);
  lv_obj_add_event_cb (Button, OnMinimize, LV_EVENT_CLICKED, Window);
  Window->MaxButton = CaptionButton (Window->Caption, NULL);
  lv_obj_add_event_cb (Window->MaxButton, OnMaximize, LV_EVENT_CLICKED, Window);
  Button = CaptionButton (Window->Caption, LV_SYMBOL_CLOSE);
  lv_obj_set_style_margin_left (Button, 4, 0);
  lv_obj_add_event_cb (Button, OnClose, LV_EVENT_CLICKED, Window);
  DrawMaxGlyph (Window);

  //
  // Client area: sunken white-ish panel the view fills.
  //
  Window->Content = lv_obj_create (Obj);
  lv_obj_remove_style_all (Window->Content);
  lv_obj_set_width (Window->Content, LV_PCT (100));
  lv_obj_set_flex_grow (Window->Content, 1);
  lv_obj_set_style_bg_color (Window->Content, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_opa (Window->Content, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Window->Content, 1, 0);
  lv_obj_set_style_border_color (Window->Content, lv_color_hex (RW_COLOR_SHADOW), 0);
  lv_obj_set_style_pad_all (Window->Content, 4, 0);
  lv_obj_set_style_text_color (Window->Content, lv_color_black (), 0);
  lv_obj_set_style_text_font (Window->Content, &lv_font_montserrat_14, 0);
  lv_obj_set_event_bubble (Window->Content, TRUE);

  //
  // Resize grip in the bottom-right corner.
  //
  Grip = lv_obj_create (Obj);
  lv_obj_remove_style_all (Grip);
  lv_obj_set_size (Grip, 14, 14);
  lv_obj_set_floating (Grip, TRUE);
  lv_obj_set_clickable (Grip, TRUE);
  lv_obj_align (Grip, LV_ALIGN_BOTTOM_RIGHT, 2, 2);
  lv_obj_add_event_cb (Grip, OnGripDrag, LV_EVENT_PRESSING, Window);
  for (Offset = 0; Offset < 2; Offset++) {
    Line = lv_line_create (Grip);
    lv_line_set_points (Line, GripPoints[Offset], 2);
    lv_obj_set_style_line_color (Line, lv_color_hex (RW_COLOR_DARK), 0);
    lv_obj_set_style_line_width (Line, 1, 0);
    lv_obj_set_clickable (Line, FALSE);
  }

  //
  // Default position: cascade from the top-left corner.
  //
  W      = DesktopWidth ();
  H      = DesktopHeight ();
  Width  = MIN (Width, W);
  Height = MIN (Height, H);
  Offset = (INT32)(mCreated % 8) * MDI_CASCADE_STEP;
  if (Offset + Width > W) {
    Offset = 0;
  }

  SetGeometry (Window, Offset, MIN (Offset, MAX (0, H - Height)), Width, Height);
  SaveRestore (Window);
  mCreated++;

  for (Link = &mFirst; *Link != NULL; Link = &(*Link)->Next) {
  }

  *Link = Window;
  RwMdiActivate (Window);
  return Window;
}

VOID
RwMdiClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_MDI_WINDOW  **Link;
  RW_MDI_WINDOW  *Top;
  lv_obj_t       *Obj;
  UINT32         Index;

  for (Link = &mFirst; *Link != NULL; Link = &(*Link)->Next) {
    if (*Link == Window) {
      *Link = Window->Next;
      break;
    }
  }

  if (Window->OnClose != NULL) {
    Window->OnClose (Window);
  }

  //
  // The close button that triggered this belongs to the window, so delete
  // asynchronously; hide it now so it is gone from the screen at once.
  //
  lv_obj_set_hidden (Window->Obj, TRUE);
  lv_obj_delete_async (Window->Obj);

  if (mActive == Window) {
    mActive = NULL;
    //
    // Activate the window that is now on top (last visible child).
    //
    Top = NULL;
    for (Index = lv_obj_get_child_count (mDesktop); Index > 0 && Top == NULL; Index--) {
      Obj = lv_obj_get_child (mDesktop, (INT32)Index - 1);
      for (Link = &mFirst; *Link != NULL; Link = &(*Link)->Next) {
        if ((*Link)->Obj == Obj) {
          Top = *Link;
          break;
        }
      }
    }

    if (Top != NULL) {
      RwMdiActivate (Top);
    } else {
      RwStatusSetWindow (NULL);
    }
  }

  FreePool (Window);
  MdiLayoutIconic ();
  RwToolbarUpdate ();
}

VOID
RwMdiCloseAll (
  VOID
  )
{
  while (mFirst != NULL) {
    RwMdiClose (mFirst);
  }

  mCreated = 0;
}

VOID
RwMdiActivate (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mActive != NULL) {
    SetCaptionColors (mActive, FALSE);
  }

  mActive = Window;
  if (Window == NULL) {
    RwStatusSetWindow (NULL);
    RwToolbarUpdate ();
    return;
  }

  SetCaptionColors (Window, TRUE);
  lv_obj_move_foreground (Window->Obj);
  RwStatusSetWindow (lv_label_get_text (Window->Title));
  RwToolbarUpdate ();
}

RW_MDI_WINDOW *
RwMdiGetActive (
  VOID
  )
{
  return mActive;
}

RW_MDI_WINDOW *
RwMdiFind (
  IN RW_COMMAND_ID  Command
  )
{
  RW_MDI_WINDOW  *Window;

  for (Window = mFirst; Window != NULL; Window = Window->Next) {
    if (Window->Command == Command) {
      return Window;
    }
  }

  return NULL;
}

UINTN
RwMdiCount (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  UINTN          Count;

  Count = 0;
  for (Window = mFirst; Window != NULL; Window = Window->Next) {
    Count++;
  }

  return Count;
}

RW_MDI_WINDOW *
RwMdiGetAt (
  IN UINTN  Index
  )
{
  RW_MDI_WINDOW  *Window;

  for (Window = mFirst; Window != NULL && Index > 0; Window = Window->Next) {
    Index--;
  }

  return Window;
}

/**
  Ctrl+Tab: activate the window below the active one in Z order (the
  bottom-most one), like the Windows MDI cycle.
**/
VOID
RwMdiActivateNext (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  lv_obj_t       *Bottom;
  UINT32         Index;

  if (mFirst == NULL) {
    return;
  }

  for (Index = 0; Index < lv_obj_get_child_count (mDesktop); Index++) {
    Bottom = lv_obj_get_child (mDesktop, (INT32)Index);
    for (Window = mFirst; Window != NULL; Window = Window->Next) {
      if ((Window->Obj == Bottom) && !lv_obj_is_hidden (Bottom)) {
        RwMdiActivate (Window);
        return;
      }
    }
  }
}

VOID
RwMdiSetMaximized (
  IN RW_MDI_WINDOW  *Window,
  IN BOOLEAN        Maximized
  )
{
  if (Window->Maximized == Maximized) {
    return;
  }

  if (Maximized) {
    if (!Window->Minimized) {
      SaveRestore (Window);
    }

    Window->Minimized = FALSE;
    lv_obj_set_hidden (Window->Content, FALSE);
    SetGeometry (Window, 0, 0, DesktopWidth (), DesktopHeight ());
  } else {
    ApplyRestore (Window);
  }

  Window->Maximized = Maximized;
  DrawMaxGlyph (Window);
  RwMdiActivate (Window);
  MdiLayoutIconic ();
}

/**
  Place the minimized windows in a row along the bottom of the desktop.
**/
STATIC
VOID
MdiLayoutIconic (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  INT32          X;
  INT32          Y;
  INT32          W;
  INT32          IconicHeight;

  W            = DesktopWidth ();
  IconicHeight = RW_CAPTION_HEIGHT + 8;
  X            = 0;
  Y            = DesktopHeight () - IconicHeight;
  for (Window = mFirst; Window != NULL; Window = Window->Next) {
    if (!Window->Minimized) {
      continue;
    }

    if ((X + MDI_ICONIC_WIDTH > W) && (X > 0)) {
      X  = 0;
      Y -= IconicHeight;
    }

    SetGeometry (Window, X, Y, MDI_ICONIC_WIDTH, IconicHeight);
    X += MDI_ICONIC_WIDTH;
  }
}

VOID
RwMdiSetMinimized (
  IN RW_MDI_WINDOW  *Window,
  IN BOOLEAN        Minimized
  )
{
  if (Window->Minimized == Minimized) {
    return;
  }

  if (Minimized) {
    if (!Window->Maximized) {
      SaveRestore (Window);
    }

    Window->Maximized = FALSE;
    Window->Minimized = TRUE;
    lv_obj_set_hidden (Window->Content, TRUE);
  } else {
    Window->Minimized = FALSE;
    lv_obj_set_hidden (Window->Content, FALSE);
    ApplyRestore (Window);
  }

  DrawMaxGlyph (Window);
  MdiLayoutIconic ();
  RwMdiActivate (Window);
}

/**
  Restore every maximized window to normal so cascade / tile can arrange it.
  Minimized windows stay where they are.
**/
STATIC
UINTN
NormalizeForArrange (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  UINTN          Count;

  Count = 0;
  for (Window = mFirst; Window != NULL; Window = Window->Next) {
    if (Window->Maximized) {
      Window->Maximized = FALSE;
      DrawMaxGlyph (Window);
    }

    if (!Window->Minimized) {
      Count++;
    }
  }

  return Count;
}

VOID
RwMdiCascade (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  INT32          W;
  INT32          H;
  INT32          Step;
  INT32          Index;

  NormalizeForArrange ();
  W     = DesktopWidth () * 2 / 3;
  H     = DesktopHeight () * 2 / 3;
  Index = 0;
  for (Window = mFirst; Window != NULL; Window = Window->Next) {
    if (Window->Minimized) {
      continue;
    }

    Step = (Index % 10) * MDI_CASCADE_STEP;
    SetGeometry (Window, Step, Step, W, H);
    SaveRestore (Window);
    lv_obj_move_foreground (Window->Obj);
    Index++;
  }

  mCreated = (UINTN)Index;
  if ((mActive != NULL) && !mActive->Minimized) {
    //
    // Cascade leaves the windows in creation order; keep the active one in
    // front, as the Windows MDI does.
    //
    RwMdiActivate (mActive);
  }
}

VOID
RwMdiTile (
  IN BOOLEAN  Horizontal
  )
{
  RW_MDI_WINDOW  *Window;
  UINTN          Count;
  INT32          Index;
  INT32          W;
  INT32          H;

  Count = NormalizeForArrange ();
  if (Count == 0) {
    return;
  }

  W     = DesktopWidth ();
  H     = DesktopHeight ();
  Index = 0;
  for (Window = mFirst; Window != NULL; Window = Window->Next) {
    if (Window->Minimized) {
      continue;
    }

    if (Horizontal) {
      // Windows stacked one above the other, each the full width.
      SetGeometry (Window, 0, Index * H / (INT32)Count, W, H / (INT32)Count);
    } else {
      // Windows side by side, each the full height.
      SetGeometry (Window, Index * W / (INT32)Count, 0, W / (INT32)Count, H);
    }

    SaveRestore (Window);
    Index++;
  }
}
