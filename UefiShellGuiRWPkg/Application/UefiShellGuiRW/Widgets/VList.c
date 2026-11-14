/** @file
  Virtual list: fixed-height rows drawn on demand inside a scrollable body,
  with an optional column header that follows the horizontal scroll.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwWidgets.h"

#define HEADER_PAD  4

STATIC
VOID
BodyContentArea (
  IN  RW_VLIST   *List,
  OUT lv_area_t  *Area
  )
{
  lv_obj_get_content_coords (List->Body, Area);
}

UINTN
RwVListPageRows (
  IN RW_VLIST  *List
  )
{
  INT32  H;

  H = lv_obj_get_content_height (List->Body);
  return (H > List->RowHeight) ? (UINTN)(H / List->RowHeight) : 1;
}

lv_color_t
RwVListRowTextColor (
  IN RW_VLIST    *List,
  IN BOOLEAN     Selected,
  IN lv_color_t  Normal
  )
{
  return (Selected && List->Base.Focused) ? lv_color_white () : Normal;
}

VOID
RwVListColumnSpan (
  IN  RW_VLIST         *List,
  IN  CONST lv_area_t  *Row,
  IN  UINTN            Index,
  OUT lv_area_t        *Cell
  )
{
  UINTN  I;
  INT32  X;

  *Cell = *Row;
  X     = Row->x1;
  for (I = 0; (I < Index) && (I < List->ColumnCount); I++) {
    X += List->Columns[I].Width;
  }

  Cell->x1 = X + HEADER_PAD;
  if ((Index + 1 < List->ColumnCount) && (List->Columns[Index].Width > 0)) {
    Cell->x2 = X + List->Columns[Index].Width - HEADER_PAD;
  }
}

STATIC
VOID
OnBodyDraw (
  lv_event_t  *Event
  )
{
  RW_VLIST    *List;
  lv_layer_t  *Layer;
  lv_area_t   Content;
  lv_area_t   Row;
  INT32       ScrollY;
  INT32       ScrollX;
  UINTN       First;
  UINTN       Index;
  INT32       Y;
  BOOLEAN     Selected;
  INT32       Width;

  List  = (RW_VLIST *)lv_event_get_user_data (Event);
  Layer = lv_event_get_layer (Event);
  if ((List->DrawRow == NULL) || (List->RowCount == 0)) {
    return;
  }

  BodyContentArea (List, &Content);
  ScrollY = lv_obj_get_scroll_y (List->Body);
  ScrollX = lv_obj_get_scroll_x (List->Body);
  First   = (ScrollY > 0) ? (UINTN)(ScrollY / List->RowHeight) : 0;
  Width   = MAX (lv_obj_get_width (List->Spacer), lv_area_get_width (&Content));
  for (Index = First; Index < List->RowCount; Index++) {
    Y = Content.y1 - ScrollY + (INT32)Index * List->RowHeight;
    if (Y > Layer->_clip_area.y2) {
      break;
    }

    Row.x1 = Content.x1 - ScrollX;
    Row.x2 = Row.x1 + Width - 1;
    Row.y1 = Y;
    Row.y2 = Y + List->RowHeight - 1;
    Selected = (BOOLEAN)(Index == List->Selected);
    if (Selected) {
      RwDrawFill (Layer, &Row, lv_color_hex (List->Base.Focused ? RW_COLOR_HILITE : RW_COLOR_SEL_INACTIVE));
    }

    List->DrawRow (List, Layer, Index, &Row, Selected);
  }
}

STATIC
VOID
OnHeaderDraw (
  lv_event_t  *Event
  )
{
  RW_VLIST          *List;
  lv_layer_t        *Layer;
  lv_area_t         Area;
  lv_area_t         Cell;
  lv_area_t         Row;
  UINTN             Index;
  lv_draw_line_dsc_t  Line;

  List  = (RW_VLIST *)lv_event_get_user_data (Event);
  Layer = lv_event_get_layer (Event);
  lv_obj_get_coords (List->Header, &Area);
  Row     = Area;
  Row.x1 -= lv_obj_get_scroll_x (List->Body) - 1;
  Row.x2  = Row.x1 + MAX (lv_obj_get_width (List->Spacer), lv_area_get_width (&Area)) - 1;
  lv_draw_line_dsc_init (&Line);
  Line.color = lv_color_hex (RW_COLOR_SHADOW);
  Line.width = 1;
  for (Index = 0; Index < List->ColumnCount; Index++) {
    RwVListColumnSpan (List, &Row, Index, &Cell);
    RwDrawText (Layer, &lv_font_montserrat_14, &Cell, List->Columns[Index].Title, lv_color_black ());
    if (List->Columns[Index].Width > 0) {
      Line.p1.x = Cell.x1 - HEADER_PAD + List->Columns[Index].Width - 1;
      Line.p2.x = Line.p1.x;
      Line.p1.y = Area.y1 + 2;
      Line.p2.y = Area.y2 - 2;
      lv_draw_line (Layer, &Line);
    }
  }
}

STATIC
VOID
OnBodyScroll (
  lv_event_t  *Event
  )
{
  RW_VLIST  *List;

  List = (RW_VLIST *)lv_event_get_user_data (Event);
  if (List->Header != NULL) {
    lv_obj_invalidate (List->Header);
  }
}

STATIC
UINTN
RowAtPoint (
  IN  RW_VLIST  *List,
  OUT INT32     *RelX
  )
{
  lv_point_t  Point;
  lv_area_t   Content;
  INT32       Y;

  lv_indev_get_point (lv_indev_active (), &Point);
  BodyContentArea (List, &Content);
  Y     = Point.y - Content.y1 + lv_obj_get_scroll_y (List->Body);
  *RelX = Point.x - Content.x1 + lv_obj_get_scroll_x (List->Body);
  if ((Y < 0) || (Point.y < Content.y1)) {
    return RW_VLIST_NONE;
  }

  if ((UINTN)(Y / List->RowHeight) >= List->RowCount) {
    return RW_VLIST_NONE;
  }

  return (UINTN)(Y / List->RowHeight);
}

STATIC
VOID
OnBodyPressed (
  lv_event_t  *Event
  )
{
  RW_VLIST  *List;
  UINTN     Row;
  INT32     X;

  List = (RW_VLIST *)lv_event_get_user_data (Event);
  if (lv_indev_active () == NULL) {
    return;
  }

  Row = RowAtPoint (List, &X);
  if (Row == RW_VLIST_NONE) {
    return;
  }

  List->ClickX = X;
  RwVListSelect (List, Row);
  if (List->OnClickRow != NULL) {
    List->OnClickRow (List, Row);
  }
}

STATIC
VOID
OnBodyDoubleClicked (
  lv_event_t  *Event
  )
{
  RW_VLIST  *List;

  List = (RW_VLIST *)lv_event_get_user_data (Event);
  if ((List->Selected != RW_VLIST_NONE) && (List->OnActivate != NULL)) {
    List->OnActivate (List, List->Selected);
  }
}

STATIC
VOID
OnDelete (
  lv_event_t  *Event
  )
{
  RW_VLIST  *List;

  List = (RW_VLIST *)lv_event_get_user_data (Event);
  RwFocusRemove (&List->Base);
  if (List->OwnerFree != NULL) {
    List->OwnerFree (List->Owner);
  }

  FreePool (List);
}

STATIC
VOID
OnFocus (
  IN RW_WIDGET  *Widget,
  IN BOOLEAN    Focused
  )
{
  RW_VLIST  *List;

  List = (RW_VLIST *)Widget;
  lv_obj_set_style_border_color (List->Base.Obj, lv_color_hex (Focused ? RW_COLOR_HILITE_EDGE : RW_COLOR_SHADOW), 0);
  lv_obj_invalidate (List->Body);
}

/**
  Left / Right: scroll the rows sideways (when they are wider than the list).
**/
STATIC
BOOLEAN
ScrollSideways (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  INT32  Step;
  INT32  Max;
  INT32  X;

  if ((Key->ScanCode != SCAN_LEFT) && (Key->ScanCode != SCAN_RIGHT)) {
    return FALSE;
  }

  lv_obj_update_layout (List->Body);
  Step = 4 * lv_font_get_glyph_width (List->Font, '0', 0);
  Max  = lv_obj_get_width (List->Spacer) - lv_obj_get_content_width (List->Body);
  X    = lv_obj_get_scroll_x (List->Body) + ((Key->ScanCode == SCAN_RIGHT) ? Step : -Step);
  X    = MAX (0, MIN (X, MAX (Max, 0)));
  lv_obj_scroll_to_x (List->Body, X, LV_ANIM_OFF);
  RwVListInvalidate (List);
  return TRUE;
}

STATIC
BOOLEAN
OnKey (
  IN RW_WIDGET                 *Widget,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  RW_VLIST  *List;
  UINTN     Page;
  UINTN     Sel;

  List = (RW_VLIST *)Widget;
  if (((Key->Modifiers & UEFI_GUI_KEY_MOD_SHIFT) != 0) && ScrollSideways (List, Key)) {
    return TRUE;
  }

  if ((List->ExtraKey != NULL) && List->ExtraKey (List, Key)) {
    return TRUE;
  }

  if (((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) == 0) && ScrollSideways (List, Key)) {
    return TRUE;
  }

  if ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) != 0) {
    return FALSE;
  }

  if (List->RowCount == 0) {
    return FALSE;
  }

  Page = RwVListPageRows (List);
  Sel  = (List->Selected == RW_VLIST_NONE) ? 0 : List->Selected;
  switch (Key->ScanCode) {
    case SCAN_UP:
      RwVListSelect (List, (Sel > 0) ? Sel - 1 : 0);
      return TRUE;
    case SCAN_DOWN:
      RwVListSelect (List, (List->Selected == RW_VLIST_NONE) ? 0 : MIN (Sel + 1, List->RowCount - 1));
      return TRUE;
    case SCAN_PAGE_UP:
      RwVListSelect (List, (Sel > Page) ? Sel - Page : 0);
      return TRUE;
    case SCAN_PAGE_DOWN:
      RwVListSelect (List, MIN (Sel + Page, List->RowCount - 1));
      return TRUE;
    case SCAN_HOME:
      RwVListSelect (List, 0);
      return TRUE;
    case SCAN_END:
      RwVListSelect (List, List->RowCount - 1);
      return TRUE;
    default:
      break;
  }

  if ((Key->UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key->UnicodeChar == CHAR_LINEFEED)) {
    if ((List->Selected != RW_VLIST_NONE) && (List->OnActivate != NULL)) {
      List->OnActivate (List, List->Selected);
    }

    return TRUE;
  }

  return FALSE;
}

RW_VLIST *
RwVListCreate (
  IN lv_obj_t         *Parent,
  IN RW_MDI_WINDOW    *Window,
  IN CONST lv_font_t  *Font
  )
{
  RW_VLIST  *List;
  lv_obj_t  *Obj;

  List = AllocateZeroPool (sizeof (RW_VLIST));
  if (List == NULL) {
    return NULL;
  }

  List->Font      = Font;
  List->RowHeight = lv_font_get_line_height (Font) + 4;
  List->Selected  = RW_VLIST_NONE;
  List->Base.Kind = "VList";
  List->Base.HandleKey    = OnKey;
  List->Base.FocusChanged = OnFocus;

  Obj = lv_obj_create (Parent);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_size (Obj, LV_PCT (100), LV_PCT (100));
  lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_WINDOW), 0);
  lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Obj, 1, 0);
  lv_obj_set_style_border_color (Obj, lv_color_hex (RW_COLOR_SHADOW), 0);
  lv_obj_set_style_pad_all (Obj, 1, 0);
  lv_obj_set_flex_flow (Obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_set_clickable (Obj, TRUE);
  lv_obj_set_event_bubble (Obj, TRUE);
  lv_obj_add_event_cb (Obj, OnDelete, LV_EVENT_DELETE, List);
  List->Base.Obj = Obj;

  Obj = lv_obj_create (List->Base.Obj);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_width (Obj, LV_PCT (100));
  lv_obj_set_flex_grow (Obj, 1);
  lv_obj_set_style_text_font (Obj, Font, 0);
  lv_obj_set_scroll_dir (Obj, LV_DIR_ALL);
  lv_obj_set_scrollbar_mode (Obj, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_clickable (Obj, TRUE);
  lv_obj_set_event_bubble (Obj, TRUE);
  lv_obj_add_event_cb (Obj, OnBodyDraw, LV_EVENT_DRAW_MAIN, List);
  lv_obj_add_event_cb (Obj, OnBodyPressed, LV_EVENT_PRESSED, List);
  lv_obj_add_event_cb (Obj, OnBodyDoubleClicked, LV_EVENT_DOUBLE_CLICKED, List);
  lv_obj_add_event_cb (Obj, OnBodyScroll, LV_EVENT_SCROLL, List);
  List->Body = Obj;

  Obj = lv_obj_create (List->Body);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_size (Obj, 1, 0);
  lv_obj_set_clickable (Obj, FALSE);
  lv_obj_set_event_bubble (Obj, TRUE);
  List->Spacer = Obj;

  RwFocusAdd (Window, &List->Base);
  return List;
}

VOID
RwVListSetColumns (
  IN RW_VLIST               *List,
  IN CONST RW_VLIST_COLUMN  *Columns,
  IN UINTN                  Count
  )
{
  lv_obj_t  *Obj;

  UINTN     Index;

  List->ColumnCount = MIN (Count, RW_VLIST_MAX_COLUMNS);
  CopyMem (List->Columns, Columns, List->ColumnCount * sizeof (RW_VLIST_COLUMN));
  for (Index = 0; Index < List->ColumnCount; Index++) {
    List->BaseWidth[Index] = List->Columns[Index].Width;
  }

  if (List->Header == NULL) {
    Obj = lv_obj_create (List->Base.Obj);
    lv_obj_remove_style_all (Obj);
    lv_obj_set_size (Obj, LV_PCT (100), lv_font_get_line_height (&lv_font_montserrat_14) + 6);
    lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_HEADER), 0);
    lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side (Obj, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width (Obj, 1, 0);
    lv_obj_set_style_border_color (Obj, lv_color_hex (RW_COLOR_SHADOW), 0);
    lv_obj_set_scrollable (Obj, FALSE);
    lv_obj_set_event_bubble (Obj, TRUE);
    lv_obj_add_event_cb (Obj, OnHeaderDraw, LV_EVENT_DRAW_MAIN, List);
    lv_obj_move_to_index (Obj, 0);
    List->Header = Obj;
  }

  lv_obj_invalidate (List->Base.Obj);
}

VOID
RwVListSetContentWidth (
  IN RW_VLIST  *List,
  IN INT32     Width
  )
{
  lv_obj_set_width (List->Spacer, MAX (Width, 1));
  if (Width <= 1) {
    lv_obj_scroll_to_x (List->Body, 0, LV_ANIM_OFF);
  }
}

VOID
RwVListFitWidth (
  IN RW_VLIST  *List,
  IN INT32     Width
  )
{
  if (Width > lv_obj_get_width (List->Spacer)) {
    lv_obj_set_width (List->Spacer, Width);
  }
}

VOID
RwVListFitCell (
  IN RW_VLIST  *List,
  IN UINTN     Index,
  IN INT32     TextWidth
  )
{
  if (Index >= List->ColumnCount) {
    return;
  }

  if ((Index + 1 < List->ColumnCount) && (List->Columns[Index].Width > 0)) {
    if (TextWidth + 2 * HEADER_PAD + 4 > List->Columns[Index].Width) {
      List->Columns[Index].Width = TextWidth + 2 * HEADER_PAD + 4;
      RwVListInvalidate (List);
    }
  } else if (Index + 1 == List->ColumnCount) {
    List->LastTextWidth = MAX (List->LastTextWidth, TextWidth);
  }

  RwVListFitWidth (List, RwVListColumnX (List, List->ColumnCount - 1) + 2 * HEADER_PAD + 8 + List->LastTextWidth);
}

VOID
RwVListResetWidth (
  IN RW_VLIST  *List
  )
{
  UINTN  Index;

  for (Index = 0; Index < List->ColumnCount; Index++) {
    List->Columns[Index].Width = List->BaseWidth[Index];
  }

  List->LastTextWidth = 0;
  lv_obj_set_width (List->Spacer, 1);
  lv_obj_scroll_to_x (List->Body, 0, LV_ANIM_OFF);
  RwVListInvalidate (List);
}

INT32
RwVListColumnX (
  IN RW_VLIST  *List,
  IN UINTN     Index
  )
{
  UINTN  I;
  INT32  X;

  X = 0;
  for (I = 0; (I < Index) && (I < List->ColumnCount); I++) {
    X += List->Columns[I].Width;
  }

  return X;
}

VOID
RwVListSetRowCount (
  IN RW_VLIST  *List,
  IN UINTN     Count
  )
{
  List->RowCount = Count;
  lv_obj_set_height (List->Spacer, (INT32)MIN (Count, 0x7FFFFF) * List->RowHeight);
  if ((List->Selected != RW_VLIST_NONE) && (List->Selected >= Count)) {
    List->Selected = (Count == 0) ? RW_VLIST_NONE : Count - 1;
  }

  lv_obj_invalidate (List->Body);
}

VOID
RwVListScrollTo (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  INT32  Top;
  INT32  Height;
  INT32  ScrollY;

  if (Row >= List->RowCount) {
    return;
  }

  lv_obj_update_layout (List->Base.Obj);
  Top     = (INT32)Row * List->RowHeight;
  Height  = lv_obj_get_content_height (List->Body);
  ScrollY = lv_obj_get_scroll_y (List->Body);
  if (Top < ScrollY) {
    lv_obj_scroll_to_y (List->Body, Top, LV_ANIM_OFF);
  } else if (Top + List->RowHeight > ScrollY + Height) {
    lv_obj_scroll_to_y (List->Body, Top + List->RowHeight - Height, LV_ANIM_OFF);
  }
}

VOID
RwVListSelect (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  if (Row >= List->RowCount) {
    return;
  }

  if (List->Selected != Row) {
    List->Selected = Row;
    if (List->OnSelect != NULL) {
      List->OnSelect (List, Row);
    }
  }

  RwVListScrollTo (List, Row);
  lv_obj_invalidate (List->Body);
}

VOID
RwVListInvalidate (
  IN RW_VLIST  *List
  )
{
  lv_obj_invalidate (List->Body);
  if (List->Header != NULL) {
    lv_obj_invalidate (List->Header);
  }
}
