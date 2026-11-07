/** @file
  Views on top of the virtual list: tree view and property table (both show
  an RW_INFO), table of string cells, read-only text view.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwWidgets.h"

#define TREE_INDENT    16
#define TREE_BOX       9
#define PROP_INDENT    12
#define TEXT_LINE_MAX  512

STATIC
VOID
RowText (
  IN  CONST RW_INFO_ROW  *Row,
  OUT RW_STR             *Str
  )
{
  RwStrAppend (Str, Row->Name);
  if (Row->Value[0] != '\0') {
    RwStrAppend (Str, (Row->Kind == RwKindField) ? ": " : "  ");
    RwStrAppend (Str, Row->Value);
  }
}

//
// ---- Tree view ---------------------------------------------------------------
//

struct _RW_TREEVIEW {
  RW_VLIST          *List;
  RW_INFO           Info;
  BOOLEAN           *HasChildren;
  BOOLEAN           *Expanded;
  UINTN             *Visible;        ///< visible row -> info row
  UINTN             VisibleCount;
  RW_INFO_ROW_FN    OnSelect;
  RW_INFO_ROW_FN    OnActivate;
  VOID              *Context;
};

STATIC
VOID
TreeFreeData (
  IN RW_TREEVIEW  *Tree
  )
{
  RwInfoFree (&Tree->Info);
  if (Tree->HasChildren != NULL) {
    FreePool (Tree->HasChildren);
  }

  if (Tree->Expanded != NULL) {
    FreePool (Tree->Expanded);
  }

  if (Tree->Visible != NULL) {
    FreePool (Tree->Visible);
  }

  Tree->HasChildren  = NULL;
  Tree->Expanded     = NULL;
  Tree->Visible      = NULL;
  Tree->VisibleCount = 0;
}

STATIC
VOID
TreeOwnerFree (
  IN VOID  *Owner
  )
{
  TreeFreeData ((RW_TREEVIEW *)Owner);
  FreePool (Owner);
}

/**
  Rebuild the visible map, keeping the selection on the same RW_INFO row
  (or the nearest shown ancestor).
**/
STATIC
VOID
TreeRebuild (
  IN RW_TREEVIEW  *Tree
  )
{
  UINTN    Index;
  UINTN    Selected;
  UINTN    NewSel;
  UINTN    Skip;
  BOOLEAN  Skipping;

  Selected = RwTreeViewGetSelected (Tree);
  Tree->VisibleCount = 0;
  Skipping           = FALSE;
  Skip               = 0;
  NewSel             = RW_VLIST_NONE;
  for (Index = 0; Index < Tree->Info.Count; Index++) {
    if (Skipping) {
      if (Tree->Info.Rows[Index].Level > Skip) {
        continue;
      }

      Skipping = FALSE;
    }

    if ((Selected != RW_VLIST_NONE) && (Index <= Selected)) {
      NewSel = Tree->VisibleCount;
    }

    Tree->Visible[Tree->VisibleCount++] = Index;
    if (Tree->HasChildren[Index] && !Tree->Expanded[Index]) {
      Skipping = TRUE;
      Skip     = Tree->Info.Rows[Index].Level;
    }
  }

  Tree->List->Selected = RW_VLIST_NONE;
  RwVListSetRowCount (Tree->List, Tree->VisibleCount);
  if (NewSel != RW_VLIST_NONE) {
    Tree->List->Selected = NewSel;
    RwVListScrollTo (Tree->List, NewSel);
  }

  RwVListInvalidate (Tree->List);
}

STATIC
VOID
TreeDrawRow (
  IN RW_VLIST         *List,
  IN lv_layer_t       *Layer,
  IN UINTN            Row,
  IN CONST lv_area_t  *Area,
  IN BOOLEAN          Selected
  )
{
  RW_TREEVIEW         *Tree;
  CONST RW_INFO_ROW   *R;
  UINTN               Index;
  lv_area_t           Box;
  lv_area_t           Text;
  lv_draw_rect_dsc_t  Rect;
  lv_draw_line_dsc_t  Line;
  RW_STR              Str;
  INT32               X;
  INT32               Cy;

  Tree  = (RW_TREEVIEW *)List->Owner;
  Index = Tree->Visible[Row];
  R     = &Tree->Info.Rows[Index];
  X     = Area->x1 + 4 + (INT32)R->Level * TREE_INDENT;
  Cy    = (Area->y1 + Area->y2) / 2;
  if (Tree->HasChildren[Index]) {
    Box.x1 = X;
    Box.y1 = Cy - TREE_BOX / 2;
    Box.x2 = X + TREE_BOX - 1;
    Box.y2 = Box.y1 + TREE_BOX - 1;
    lv_draw_rect_dsc_init (&Rect);
    Rect.bg_color     = lv_color_white ();
    Rect.bg_opa       = LV_OPA_COVER;
    Rect.border_color = lv_color_hex (RW_COLOR_TEXT_GRAY);
    Rect.border_width = 1;
    lv_draw_rect (Layer, &Rect, &Box);
    lv_draw_line_dsc_init (&Line);
    Line.color = lv_color_black ();
    Line.width = 1;
    Line.p1.x  = Box.x1 + 2;
    Line.p2.x  = Box.x2 - 1;
    Line.p1.y  = Cy;
    Line.p2.y  = Cy;
    lv_draw_line (Layer, &Line);
    if (!Tree->Expanded[Index]) {
      Line.p1.x = (Box.x1 + Box.x2) / 2;
      Line.p2.x = Line.p1.x;
      Line.p1.y = Box.y1 + 2;
      Line.p2.y = Box.y2 - 1;
      lv_draw_line (Layer, &Line);
    }
  }

  Text    = *Area;
  Text.x1 = X + TREE_BOX + 6;
  RwStrInit (&Str);
  RowText (R, &Str);
  RwDrawText (
    Layer,
    (R->Kind == RwKindText) ? RwMonoFont () : List->Font,
    &Text,
    Str.Buf,
    RwVListRowTextColor (List, Selected, RwEmphasisColor (R->Emphasis))
    );
  RwStrFree (&Str);
}

STATIC
VOID
TreeToggle (
  IN RW_TREEVIEW  *Tree,
  IN UINTN        Index,
  IN BOOLEAN      Expand
  )
{
  if (!Tree->HasChildren[Index] || (Tree->Expanded[Index] == Expand)) {
    return;
  }

  Tree->Expanded[Index] = Expand;
  TreeRebuild (Tree);
}

STATIC
VOID
TreeOnSelect (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  RW_TREEVIEW  *Tree;

  Tree = (RW_TREEVIEW *)List->Owner;
  if ((Tree->OnSelect != NULL) && (Row < Tree->VisibleCount)) {
    Tree->OnSelect (Tree, &Tree->Info.Rows[Tree->Visible[Row]], Tree->Visible[Row], Tree->Context);
  }
}

STATIC
VOID
TreeOnActivate (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  RW_TREEVIEW  *Tree;
  UINTN        Index;

  Tree = (RW_TREEVIEW *)List->Owner;
  if (Row >= Tree->VisibleCount) {
    return;
  }

  Index = Tree->Visible[Row];
  if (Tree->OnActivate != NULL) {
    Tree->OnActivate (Tree, &Tree->Info.Rows[Index], Index, Tree->Context);
  } else if (Tree->HasChildren[Index]) {
    TreeToggle (Tree, Index, (BOOLEAN)!Tree->Expanded[Index]);
  }
}

STATIC
VOID
TreeOnClickRow (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  RW_TREEVIEW  *Tree;
  UINTN        Index;
  INT32        X;

  Tree  = (RW_TREEVIEW *)List->Owner;
  Index = Tree->Visible[Row];
  X     = 4 + (INT32)Tree->Info.Rows[Index].Level * TREE_INDENT;
  if (Tree->HasChildren[Index] && (List->ClickX >= X - 3) && (List->ClickX <= X + TREE_BOX + 3)) {
    TreeToggle (Tree, Index, (BOOLEAN)!Tree->Expanded[Index]);
  }
}

STATIC
BOOLEAN
TreeKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  RW_TREEVIEW  *Tree;
  UINTN        Index;
  UINTN        Row;

  Tree = (RW_TREEVIEW *)List->Owner;
  if ((List->Selected == RW_VLIST_NONE) || (List->Selected >= Tree->VisibleCount)) {
    return FALSE;
  }

  Row   = List->Selected;
  Index = Tree->Visible[Row];
  if ((Key->ScanCode == SCAN_RIGHT) || RwKeyIsChar (Key, L'+')) {
    if (Tree->HasChildren[Index] && !Tree->Expanded[Index]) {
      TreeToggle (Tree, Index, TRUE);
    } else if (Tree->HasChildren[Index] && (Key->ScanCode == SCAN_RIGHT)) {
      RwVListSelect (List, Row + 1);
    }

    return TRUE;
  }

  if ((Key->ScanCode == SCAN_LEFT) || RwKeyIsChar (Key, L'-')) {
    if (Tree->HasChildren[Index] && Tree->Expanded[Index]) {
      TreeToggle (Tree, Index, FALSE);
    } else if (Key->ScanCode == SCAN_LEFT) {
      //
      // Go to the parent: the nearest row above with a smaller level.
      //
      while (Row > 0) {
        Row--;
        if (Tree->Info.Rows[Tree->Visible[Row]].Level < Tree->Info.Rows[Index].Level) {
          RwVListSelect (List, Row);
          break;
        }
      }
    }

    return TRUE;
  }

  return FALSE;
}

RW_TREEVIEW *
RwTreeViewCreate (
  IN lv_obj_t       *Parent,
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_TREEVIEW  *Tree;

  Tree = AllocateZeroPool (sizeof (RW_TREEVIEW));
  if (Tree == NULL) {
    return NULL;
  }

  Tree->List = RwVListCreate (Parent, Window, &lv_font_montserrat_14);
  if (Tree->List == NULL) {
    FreePool (Tree);
    return NULL;
  }

  Tree->List->Base.Kind  = "TreeView";
  Tree->List->Owner      = Tree;
  Tree->List->OwnerFree  = TreeOwnerFree;
  Tree->List->DrawRow    = TreeDrawRow;
  Tree->List->OnSelect   = TreeOnSelect;
  Tree->List->OnActivate = TreeOnActivate;
  Tree->List->OnClickRow = TreeOnClickRow;
  Tree->List->ExtraKey   = TreeKey;
  Tree->List->RowHeight  = MAX (Tree->List->RowHeight, lv_font_get_line_height (RwMonoFont ()) + 3);
  return Tree;
}

RW_VLIST *
RwTreeViewList (
  IN RW_TREEVIEW  *Tree
  )
{
  return Tree->List;
}

VOID
RwTreeViewSetInfo (
  IN RW_TREEVIEW  *Tree,
  IN RW_INFO      *Info,
  IN UINTN        ExpandLevels
  )
{
  UINTN  Index;
  UINTN  Count;

  TreeFreeData (Tree);
  Tree->Info = *Info;
  ZeroMem (Info, sizeof (*Info));
  Count             = MAX (Tree->Info.Count, 1);
  Tree->HasChildren = AllocateZeroPool (Count * sizeof (BOOLEAN));
  Tree->Expanded    = AllocateZeroPool (Count * sizeof (BOOLEAN));
  Tree->Visible     = AllocateZeroPool (Count * sizeof (UINTN));
  if ((Tree->HasChildren == NULL) || (Tree->Expanded == NULL) || (Tree->Visible == NULL)) {
    TreeFreeData (Tree);
    RwVListSetRowCount (Tree->List, 0);
    return;
  }

  for (Index = 0; Index < Tree->Info.Count; Index++) {
    Tree->HasChildren[Index] = (BOOLEAN)((Index + 1 < Tree->Info.Count) &&
                                         (Tree->Info.Rows[Index + 1].Level > Tree->Info.Rows[Index].Level));
    Tree->Expanded[Index] = (BOOLEAN)(Tree->Info.Rows[Index].Level < ExpandLevels);
  }

  Tree->List->Selected = RW_VLIST_NONE;
  TreeRebuild (Tree);
  if (Tree->VisibleCount > 0) {
    RwVListSelect (Tree->List, 0);
  }
}

VOID
RwTreeViewSetCallbacks (
  IN RW_TREEVIEW     *Tree,
  IN RW_INFO_ROW_FN  OnSelect    OPTIONAL,
  IN RW_INFO_ROW_FN  OnActivate  OPTIONAL,
  IN VOID            *Context
  )
{
  Tree->OnSelect   = OnSelect;
  Tree->OnActivate = OnActivate;
  Tree->Context    = Context;
}

UINTN
RwTreeViewVisibleCount (
  IN RW_TREEVIEW  *Tree
  )
{
  return Tree->VisibleCount;
}

UINTN
RwTreeViewGetSelected (
  IN RW_TREEVIEW  *Tree
  )
{
  if ((Tree->List->Selected == RW_VLIST_NONE) || (Tree->List->Selected >= Tree->VisibleCount)) {
    return RW_VLIST_NONE;
  }

  return Tree->Visible[Tree->List->Selected];
}

BOOLEAN
RwTreeViewSelectTag (
  IN RW_TREEVIEW  *Tree,
  IN INT32        Tag
  )
{
  UINTN  Index;
  UINTN  Up;
  UINT8  Level;
  UINTN  Row;

  for (Index = 0; Index < Tree->Info.Count; Index++) {
    if (Tree->Info.Rows[Index].Tag != Tag) {
      continue;
    }

    //
    // Expand every ancestor.
    //
    Level = Tree->Info.Rows[Index].Level;
    for (Up = Index; (Up > 0) && (Level > 0); Up--) {
      if (Tree->Info.Rows[Up - 1].Level < Level) {
        Level                   = Tree->Info.Rows[Up - 1].Level;
        Tree->Expanded[Up - 1]  = TRUE;
      }
    }

    TreeRebuild (Tree);
    for (Row = 0; Row < Tree->VisibleCount; Row++) {
      if (Tree->Visible[Row] == Index) {
        RwVListSelect (Tree->List, Row);
        return TRUE;
      }
    }
  }

  return FALSE;
}

VOID
RwTreeViewExpandAll (
  IN RW_TREEVIEW  *Tree,
  IN BOOLEAN      Expand
  )
{
  UINTN  Index;

  for (Index = 0; Index < Tree->Info.Count; Index++) {
    Tree->Expanded[Index] = Expand;
  }

  TreeRebuild (Tree);
}

//
// ---- Property table ------------------------------------------------------------
//

struct _RW_PROPTABLE {
  RW_VLIST          *List;
  RW_INFO           Info;
  BOOLEAN           ShowOffset;
  RW_INFO_ROW_FN    OnSelect;
  RW_INFO_ROW_FN    OnActivate;
  VOID              *Context;
};

STATIC
VOID
PropOwnerFree (
  IN VOID  *Owner
  )
{
  RW_PROPTABLE  *Table;

  Table = (RW_PROPTABLE *)Owner;
  RwInfoFree (&Table->Info);
  FreePool (Table);
}

STATIC
VOID
PropDrawRow (
  IN RW_VLIST         *List,
  IN lv_layer_t       *Layer,
  IN UINTN            Row,
  IN CONST lv_area_t  *Area,
  IN BOOLEAN          Selected
  )
{
  RW_PROPTABLE       *Table;
  CONST RW_INFO_ROW  *R;
  lv_area_t          Cell;
  lv_area_t          Line;
  RW_STR             Str;
  CHAR8              Offset[16];
  UINTN              ValueColumn;

  Table       = (RW_PROPTABLE *)List->Owner;
  R           = &Table->Info.Rows[Row];
  ValueColumn = Table->ShowOffset ? 2 : 1;
  switch (R->Kind) {
    case RwKindSection:
      if (!Selected) {
        RwDrawFill (Layer, Area, lv_color_hex (RW_COLOR_SECTION));
      }

      Cell     = *Area;
      Cell.x1 += 4 + (INT32)R->Level * PROP_INDENT;
      RwStrInit (&Str);
      RowText (R, &Str);
      RwDrawText (Layer, List->Font, &Cell, Str.Buf, RwVListRowTextColor (List, Selected, lv_color_hex (RW_COLOR_STRONG)));
      RwStrFree (&Str);
      break;

    case RwKindField:
      RwVListColumnSpan (List, Area, 0, &Cell);
      Cell.x1 += (INT32)(R->Level > 0 ? R->Level - 1 : 0) * PROP_INDENT;
      RwDrawText (Layer, List->Font, &Cell, R->Name, RwVListRowTextColor (List, Selected, lv_color_black ()));
      if (Table->ShowOffset && (R->Offset != RW_INFO_NO_OFFSET)) {
        RwVListColumnSpan (List, Area, 1, &Cell);
        AsciiSPrint (Offset, sizeof (Offset), "%03X", R->Offset);
        RwDrawText (Layer, RwMonoFont (), &Cell, Offset, RwVListRowTextColor (List, Selected, lv_color_hex (RW_COLOR_TEXT_GRAY)));
      }

      RwVListColumnSpan (List, Area, ValueColumn, &Cell);
      RwDrawText (Layer, RwMonoFont (), &Cell, R->Value, RwVListRowTextColor (List, Selected, RwEmphasisColor (R->Emphasis)));
      break;

    default:
      Cell     = *Area;
      Cell.x1 += 4 + (INT32)R->Level * PROP_INDENT;
      RwDrawText (Layer, RwMonoFont (), &Cell, R->Name, RwVListRowTextColor (List, Selected, RwEmphasisColor (R->Emphasis)));
      break;
  }

  //
  // Light grid line under each row.
  //
  Line    = *Area;
  Line.y1 = Area->y2;
  RwDrawFill (Layer, &Line, lv_color_hex (RW_COLOR_GRID_LINE));
}

STATIC
VOID
PropOnSelect (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  RW_PROPTABLE  *Table;

  Table = (RW_PROPTABLE *)List->Owner;
  if ((Table->OnSelect != NULL) && (Row < Table->Info.Count)) {
    Table->OnSelect (Table, &Table->Info.Rows[Row], Row, Table->Context);
  }
}

STATIC
VOID
PropOnActivate (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  RW_PROPTABLE  *Table;

  Table = (RW_PROPTABLE *)List->Owner;
  if ((Table->OnActivate != NULL) && (Row < Table->Info.Count)) {
    Table->OnActivate (Table, &Table->Info.Rows[Row], Row, Table->Context);
  }
}

RW_PROPTABLE *
RwPropTableCreate (
  IN lv_obj_t       *Parent,
  IN RW_MDI_WINDOW  *Window,
  IN BOOLEAN        ShowOffset
  )
{
  RW_PROPTABLE                   *Table;
  STATIC CONST RW_VLIST_COLUMN   WithOffset[] = { { "Field", 200 }, { "Offset", 60 }, { "Value", 0 } };
  STATIC CONST RW_VLIST_COLUMN   Plain[]      = { { "Field", 200 }, { "Value", 0 } };

  Table = AllocateZeroPool (sizeof (RW_PROPTABLE));
  if (Table == NULL) {
    return NULL;
  }

  Table->List = RwVListCreate (Parent, Window, &lv_font_montserrat_14);
  if (Table->List == NULL) {
    FreePool (Table);
    return NULL;
  }

  Table->ShowOffset        = ShowOffset;
  Table->List->Base.Kind   = "PropTable";
  Table->List->Owner       = Table;
  Table->List->OwnerFree   = PropOwnerFree;
  Table->List->DrawRow     = PropDrawRow;
  Table->List->OnSelect    = PropOnSelect;
  Table->List->OnActivate  = PropOnActivate;
  Table->List->RowHeight   = MAX (Table->List->RowHeight, lv_font_get_line_height (RwMonoFont ()) + 3);
  if (ShowOffset) {
    RwVListSetColumns (Table->List, WithOffset, ARRAY_SIZE (WithOffset));
  } else {
    RwVListSetColumns (Table->List, Plain, ARRAY_SIZE (Plain));
  }

  return Table;
}

RW_VLIST *
RwPropTableList (
  IN RW_PROPTABLE  *Table
  )
{
  return Table->List;
}

VOID
RwPropTableSetInfo (
  IN RW_PROPTABLE  *Table,
  IN RW_INFO       *Info
  )
{
  RwInfoFree (&Table->Info);
  Table->Info = *Info;
  ZeroMem (Info, sizeof (*Info));
  Table->List->Selected = RW_VLIST_NONE;
  RwVListSetRowCount (Table->List, Table->Info.Count);
  lv_obj_scroll_to_y (Table->List->Body, 0, LV_ANIM_OFF);
  RwVListInvalidate (Table->List);
}

VOID
RwPropTableSetCallbacks (
  IN RW_PROPTABLE    *Table,
  IN RW_INFO_ROW_FN  OnSelect    OPTIONAL,
  IN RW_INFO_ROW_FN  OnActivate  OPTIONAL,
  IN VOID            *Context
  )
{
  Table->OnSelect   = OnSelect;
  Table->OnActivate = OnActivate;
  Table->Context    = Context;
}

CONST RW_INFO *
RwPropTableGetInfo (
  IN RW_PROPTABLE  *Table
  )
{
  return &Table->Info;
}

//
// ---- Table -------------------------------------------------------------------
//

typedef struct {
  CHAR8    **Cells;      ///< ColumnCount pointers, then the strings, one allocation
  INT32    Tag;
  UINT8    Emphasis;
} TABLE_ROW;

struct _RW_TABLE {
  RW_VLIST           *List;
  BOOLEAN            Mono;
  TABLE_ROW          *Rows;
  UINTN              Count;
  UINTN              Capacity;
  RW_TABLE_ROW_FN    OnSelect;
  RW_TABLE_ROW_FN    OnActivate;
  VOID               *Context;
};

STATIC
VOID
TableFreeRows (
  IN RW_TABLE  *Table
  )
{
  UINTN  Index;

  for (Index = 0; Index < Table->Count; Index++) {
    FreePool (Table->Rows[Index].Cells);
  }

  if (Table->Rows != NULL) {
    FreePool (Table->Rows);
  }

  Table->Rows     = NULL;
  Table->Count    = 0;
  Table->Capacity = 0;
}

STATIC
VOID
TableOwnerFree (
  IN VOID  *Owner
  )
{
  TableFreeRows ((RW_TABLE *)Owner);
  FreePool (Owner);
}

STATIC
VOID
TableDrawRow (
  IN RW_VLIST         *List,
  IN lv_layer_t       *Layer,
  IN UINTN            Row,
  IN CONST lv_area_t  *Area,
  IN BOOLEAN          Selected
  )
{
  RW_TABLE   *Table;
  TABLE_ROW  *R;
  UINTN      Column;
  lv_area_t  Cell;

  Table = (RW_TABLE *)List->Owner;
  R     = &Table->Rows[Row];
  for (Column = 0; Column < List->ColumnCount; Column++) {
    RwVListColumnSpan (List, Area, Column, &Cell);
    RwDrawText (
      Layer,
      Table->Mono ? RwMonoFont () : List->Font,
      &Cell,
      R->Cells[Column],
      RwVListRowTextColor (List, Selected, RwEmphasisColor (R->Emphasis))
      );
  }
}

STATIC
VOID
TableOnSelect (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  RW_TABLE  *Table;

  Table = (RW_TABLE *)List->Owner;
  if ((Table->OnSelect != NULL) && (Row < Table->Count)) {
    Table->OnSelect (Table, Row, Table->Rows[Row].Tag, Table->Context);
  }
}

STATIC
VOID
TableOnActivate (
  IN RW_VLIST  *List,
  IN UINTN     Row
  )
{
  RW_TABLE  *Table;

  Table = (RW_TABLE *)List->Owner;
  if ((Table->OnActivate != NULL) && (Row < Table->Count)) {
    Table->OnActivate (Table, Row, Table->Rows[Row].Tag, Table->Context);
  }
}

RW_TABLE *
RwTableCreate (
  IN lv_obj_t               *Parent,
  IN RW_MDI_WINDOW          *Window,
  IN CONST RW_VLIST_COLUMN  *Columns,
  IN UINTN                  ColumnCount,
  IN BOOLEAN                Mono
  )
{
  RW_TABLE  *Table;

  Table = AllocateZeroPool (sizeof (RW_TABLE));
  if (Table == NULL) {
    return NULL;
  }

  Table->List = RwVListCreate (Parent, Window, &lv_font_montserrat_14);
  if (Table->List == NULL) {
    FreePool (Table);
    return NULL;
  }

  Table->Mono             = Mono;
  Table->List->Base.Kind  = "Table";
  Table->List->Owner      = Table;
  Table->List->OwnerFree  = TableOwnerFree;
  Table->List->DrawRow    = TableDrawRow;
  Table->List->OnSelect   = TableOnSelect;
  Table->List->OnActivate = TableOnActivate;
  if (Mono) {
    Table->List->RowHeight = MAX (Table->List->RowHeight, lv_font_get_line_height (RwMonoFont ()) + 3);
  }

  RwVListSetColumns (Table->List, Columns, ColumnCount);
  return Table;
}

RW_VLIST *
RwTableList (
  IN RW_TABLE  *Table
  )
{
  return Table->List;
}

VOID
RwTableClear (
  IN RW_TABLE  *Table
  )
{
  TableFreeRows (Table);
  Table->List->Selected = RW_VLIST_NONE;
  RwVListSetRowCount (Table->List, 0);
}

STATIC
CHAR8 **
TableBuildCells (
  IN CONST CHAR8  **Cells,
  IN UINTN        Columns
  )
{
  UINTN  Size;
  UINTN  Column;
  UINTN  Len;
  CHAR8  **Block;
  CHAR8  *Text;

  Size = Columns * sizeof (CHAR8 *);
  for (Column = 0; Column < Columns; Column++) {
    Size += AsciiStrLen ((Cells[Column] != NULL) ? Cells[Column] : "") + 1;
  }

  Block = AllocatePool (Size);
  if (Block == NULL) {
    return NULL;
  }

  Text = (CHAR8 *)(Block + Columns);
  for (Column = 0; Column < Columns; Column++) {
    Len = AsciiStrLen ((Cells[Column] != NULL) ? Cells[Column] : "");
    CopyMem (Text, (Cells[Column] != NULL) ? Cells[Column] : "", Len);
    Text[Len]     = '\0';
    Block[Column] = Text;
    Text         += Len + 1;
  }

  return Block;
}

EFI_STATUS
RwTableAddRow (
  IN RW_TABLE     *Table,
  IN CONST CHAR8  **Cells,
  IN INT32        Tag,
  IN UINT8        Emphasis
  )
{
  CHAR8      **Block;
  TABLE_ROW  *Grown;

  if (Table->Count == Table->Capacity) {
    Grown = ReallocatePool (
              Table->Capacity * sizeof (TABLE_ROW),
              (Table->Capacity + 64) * sizeof (TABLE_ROW),
              Table->Rows
              );
    if (Grown == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Table->Rows      = Grown;
    Table->Capacity += 64;
  }

  Block = TableBuildCells (Cells, Table->List->ColumnCount);
  if (Block == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Table->Rows[Table->Count].Cells    = Block;
  Table->Rows[Table->Count].Tag      = Tag;
  Table->Rows[Table->Count].Emphasis = Emphasis;
  Table->Count++;
  RwVListSetRowCount (Table->List, Table->Count);
  return EFI_SUCCESS;
}

EFI_STATUS
RwTableSetRow (
  IN RW_TABLE     *Table,
  IN UINTN        Row,
  IN CONST CHAR8  **Cells,
  IN UINT8        Emphasis
  )
{
  CHAR8  **Block;

  if (Row >= Table->Count) {
    return EFI_INVALID_PARAMETER;
  }

  Block = TableBuildCells (Cells, Table->List->ColumnCount);
  if (Block == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  FreePool (Table->Rows[Row].Cells);
  Table->Rows[Row].Cells    = Block;
  Table->Rows[Row].Emphasis = Emphasis;
  RwVListInvalidate (Table->List);
  return EFI_SUCCESS;
}

INT32
RwTableGetTag (
  IN RW_TABLE  *Table,
  IN UINTN     Row
  )
{
  return (Row < Table->Count) ? Table->Rows[Row].Tag : -1;
}

UINTN
RwTableRowCount (
  IN RW_TABLE  *Table
  )
{
  return Table->Count;
}

CONST CHAR8 *
RwTableGetCell (
  IN RW_TABLE  *Table,
  IN UINTN     Row,
  IN UINTN     Column
  )
{
  if ((Row >= Table->Count) || (Column >= Table->List->ColumnCount)) {
    return NULL;
  }

  return Table->Rows[Row].Cells[Column];
}

VOID
RwTableSetCallbacks (
  IN RW_TABLE         *Table,
  IN RW_TABLE_ROW_FN  OnSelect    OPTIONAL,
  IN RW_TABLE_ROW_FN  OnActivate  OPTIONAL,
  IN VOID             *Context
  )
{
  Table->OnSelect   = OnSelect;
  Table->OnActivate = OnActivate;
  Table->Context    = Context;
}

//
// ---- Text view -------------------------------------------------------------------
//

struct _RW_TEXTVIEW {
  RW_VLIST    *List;
  CHAR8       *Text;
  UINTN       *Lines;      ///< offset of each line
  UINTN       LineCount;
};

STATIC
VOID
TextFreeData (
  IN RW_TEXTVIEW  *View
  )
{
  if (View->Text != NULL) {
    FreePool (View->Text);
  }

  if (View->Lines != NULL) {
    FreePool (View->Lines);
  }

  View->Text      = NULL;
  View->Lines     = NULL;
  View->LineCount = 0;
}

STATIC
VOID
TextOwnerFree (
  IN VOID  *Owner
  )
{
  TextFreeData ((RW_TEXTVIEW *)Owner);
  FreePool (Owner);
}

/**
  Copy line Row into Buffer with tabs expanded and '\r' dropped.
**/
STATIC
UINTN
TextExpandLine (
  IN  RW_TEXTVIEW  *View,
  IN  UINTN        Row,
  OUT CHAR8        *Buffer,
  IN  UINTN        BufferSize
  )
{
  CONST CHAR8  *P;
  UINTN        Out;

  Out = 0;
  for (P = View->Text + View->Lines[Row]; (*P != '\0') && (*P != '\n') && (Out + 1 < BufferSize); P++) {
    if (*P == '\r') {
      continue;
    }

    if (*P == '\t') {
      do {
        Buffer[Out++] = ' ';
      } while (((Out % 4) != 0) && (Out + 1 < BufferSize));

      continue;
    }

    Buffer[Out++] = ((*P >= 0x20) && (*P < 0x7F)) ? *P : '.';
  }

  Buffer[Out] = '\0';
  return Out;
}

STATIC
VOID
TextDrawRow (
  IN RW_VLIST         *List,
  IN lv_layer_t       *Layer,
  IN UINTN            Row,
  IN CONST lv_area_t  *Area,
  IN BOOLEAN          Selected
  )
{
  RW_TEXTVIEW  *View;
  CHAR8        Line[TEXT_LINE_MAX];
  lv_area_t    Cell;

  View = (RW_TEXTVIEW *)List->Owner;
  TextExpandLine (View, Row, Line, sizeof (Line));
  Cell     = *Area;
  Cell.x1 += 4;
  RwDrawText (Layer, List->Font, &Cell, Line, RwVListRowTextColor (List, Selected, lv_color_black ()));
}

RW_TEXTVIEW *
RwTextViewCreate (
  IN lv_obj_t       *Parent,
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_TEXTVIEW  *View;

  View = AllocateZeroPool (sizeof (RW_TEXTVIEW));
  if (View == NULL) {
    return NULL;
  }

  View->List = RwVListCreate (Parent, Window, RwMonoFont ());
  if (View->List == NULL) {
    FreePool (View);
    return NULL;
  }

  View->List->Base.Kind = "TextView";
  View->List->Owner     = View;
  View->List->OwnerFree = TextOwnerFree;
  View->List->DrawRow   = TextDrawRow;
  View->List->RowHeight = lv_font_get_line_height (RwMonoFont ()) + 1;
  return View;
}

RW_VLIST *
RwTextViewList (
  IN RW_TEXTVIEW  *View
  )
{
  return View->List;
}

VOID
RwTextViewSetText (
  IN RW_TEXTVIEW  *View,
  IN CHAR8        *Text
  )
{
  UINTN        Count;
  UINTN        Index;
  UINTN        Longest;
  UINTN        Length;
  CHAR8        Line[TEXT_LINE_MAX];
  CONST CHAR8  *P;

  TextFreeData (View);
  View->Text = Text;
  Count      = 1;
  for (P = Text; (P != NULL) && (*P != '\0'); P++) {
    if (*P == '\n') {
      Count++;
    }
  }

  View->Lines = AllocatePool (Count * sizeof (UINTN));
  if ((Text == NULL) || (View->Lines == NULL)) {
    TextFreeData (View);
    RwVListSetRowCount (View->List, 0);
    return;
  }

  View->Lines[0] = 0;
  Index          = 1;
  for (P = Text; *P != '\0'; P++) {
    if ((*P == '\n') && (P[1] != '\0')) {
      View->Lines[Index++] = (UINTN)(P - Text) + 1;
    }
  }

  View->LineCount = Index;
  Longest         = 0;
  for (Index = 0; Index < View->LineCount; Index++) {
    Length  = TextExpandLine (View, Index, Line, sizeof (Line));
    Longest = MAX (Longest, Length);
  }

  RwVListSetContentWidth (View->List, (INT32)(Longest + 2) * lv_font_get_glyph_width (RwMonoFont (), '0', 0));
  View->List->Selected = RW_VLIST_NONE;
  RwVListSetRowCount (View->List, View->LineCount);
}

UINTN
RwTextViewLineCount (
  IN RW_TEXTVIEW  *View
  )
{
  return View->LineCount;
}
