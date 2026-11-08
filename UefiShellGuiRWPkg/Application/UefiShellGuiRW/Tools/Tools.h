/** @file
  Phase 6 tools: file access, screenshot, dump, window save, snapshots and
  the file browser / editor windows.

  Files the tools create go next to UefiShellGuiRW.efi (the directory it was
  started from) when that volume is writable, else to the root of the first
  writable volume. Names are PREFIXnnnn.EXT (8.3), numbered from 0001.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_TOOLS_H_
#define RW_TOOLS_H_

#include "../Views/Views.h"

//
// ---- File.c ------------------------------------------------------------------
//
typedef struct {
  EFI_HANDLE    Handle;
  CHAR8         Label[40];       ///< volume label, "" when none
  CHAR8         Path[64];        ///< end of the device path text
  BOOLEAN       ReadOnly;
  BOOLEAN       IsImage;         ///< UefiShellGuiRW.efi was loaded from it
  UINT64        Size;
  UINT64        Free;
} RW_VOLUME;

typedef struct {
  CHAR16      *Name;
  UINT64      Size;
  BOOLEAN     IsDir;
  EFI_TIME    Time;
} RW_DIR_ENTRY;

/** Every simple file system volume, the image volume first. FreePool *Vols. **/
EFI_STATUS
RwVolumeList (
  OUT RW_VOLUME  **Vols,
  OUT UINTN      *Count
  );

/** Entries of Dir ("\" = root) without "." and "..", folders first, by name. **/
EFI_STATUS
RwDirList (
  IN  EFI_HANDLE    Vol,
  IN  CONST CHAR16  *Dir,
  OUT RW_DIR_ENTRY  **Entries,
  OUT UINTN         *Count
  );

VOID
RwDirFree (
  IN RW_DIR_ENTRY  *Entries,
  IN UINTN         Count
  );

/**
  Read a whole file (FreePool *Data). EFI_BAD_BUFFER_SIZE when it is larger
  than MaxSize (*Size then holds its size).
**/
EFI_STATUS
RwFileRead (
  IN  EFI_HANDLE    Vol,
  IN  CONST CHAR16  *Path,
  IN  UINTN         MaxSize,
  OUT UINT8         **Data,
  OUT UINTN         *Size
  );

/** Create or replace a file. **/
EFI_STATUS
RwFileWrite (
  IN EFI_HANDLE    Vol,
  IN CONST CHAR16  *Path,
  IN CONST VOID    *Data,
  IN UINTN         Size
  );

BOOLEAN
RwFileExists (
  IN EFI_HANDLE    Vol,
  IN CONST CHAR16  *Path
  );

/** Where the tools save: volume and directory ("\" or "\EFI\Tools\"). **/
EFI_STATUS
RwSaveTarget (
  OUT EFI_HANDLE  *Vol,
  OUT CHAR16      *Dir,
  IN  UINTN       DirChars
  );

/**
  First PREFIXnnnn free for every extension in Exts (1 or 2), as "PREFIXnnnn"
  without extension.
**/
EFI_STATUS
RwSaveFindFree (
  IN  CONST CHAR8  *Prefix,
  IN  CONST CHAR8  *Ext1,
  IN  CONST CHAR8  *Ext2  OPTIONAL,
  OUT CHAR8        *Base,
  IN  UINTN        BaseSize
  );

/** Write FileName ("SCR0001.BMP") in the save directory. **/
EFI_STATUS
RwSaveWrite (
  IN CONST CHAR8  *FileName,
  IN CONST VOID   *Data,
  IN UINTN        Size
  );

/** Read FileName from the save directory. **/
EFI_STATUS
RwSaveRead (
  IN  CONST CHAR8  *FileName,
  IN  UINTN        MaxSize,
  OUT UINT8        **Data,
  OUT UINTN        *Size
  );

/** Find a free PREFIXnnnn.EXT and write it; Name receives "PREFIXnnnn.EXT". **/
EFI_STATUS
RwSaveNext (
  IN  CONST CHAR8  *Prefix,
  IN  CONST CHAR8  *Ext,
  IN  CONST VOID   *Data,
  IN  UINTN        Size,
  OUT CHAR8        *Name,
  IN  UINTN        NameSize
  );

//
// ---- Tools.c -----------------------------------------------------------------
//

/** File > Save Screenshot (F12): the whole screen as SCRnnnn.BMP. **/
EFI_STATUS
RwToolScreenshot (
  VOID
  );

/** File > Dump All to File: DUMPnnnn.TXT. **/
EFI_STATUS
RwToolDumpAll (
  VOID
  );

/**
  File > Save Window (Ctrl+S): the window's own OnSave, else a hex range of
  its grid (asks start and length; HEXnnnn.TXT + .BIN), else the text of its
  lists (SAVEnnnn.TXT, or the window's SavePrefix).
**/
VOID
RwToolSaveWindow (
  IN RW_MDI_WINDOW  *Window
  );

/** Save Text as PREFIXnnnn.TXT and report it on the status bar. **/
EFI_STATUS
RwToolSaveText (
  IN CONST CHAR8  *Prefix,
  IN CONST CHAR8  *Text,
  IN UINTN        Length
  );

/** Name of the last file a tool saved ("" before the first), for the self test. **/
CONST CHAR8 *
RwToolLastSaved (
  VOID
  );

//
// ---- WriteLogView.c ------------------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW    *Window;
  RW_TABLE         *Table;
  lv_obj_t         *Heading;
  lv_timer_t       *Timer;
  UINT64           Shown;     ///< RwWlogTotal () when last filled
} RW_WLOG_VIEW;

RW_MDI_WINDOW *
RwViewOpenWriteLog (
  VOID
  );

RW_WLOG_VIEW *
RwWlogViewGet (
  VOID
  );

//
// ---- FileView.c / FileEdit.c ---------------------------------------------------
//
#define RW_FILE_PATH_MAX   260
#define RW_TEXT_EDIT_MAX   SIZE_64KB      // larger files open in the hex editor
#define RW_HEX_EDIT_MAX    SIZE_16MB

typedef struct {
  RW_MDI_WINDOW    *Window;
  RW_TABLE         *Volumes;
  RW_TABLE         *Entries;
  lv_obj_t         *Heading;
  RW_VOLUME        *Vols;
  UINTN            VolCount;
  INT32            Vol;                      ///< index in Vols, -1 before one is chosen
  CHAR16           Dir[RW_FILE_PATH_MAX];
  RW_DIR_ENTRY     *List;
  UINTN            ListCount;
} RW_FILE_VIEW;

RW_MDI_WINDOW *
RwViewOpenFiles (
  VOID
  );

RW_FILE_VIEW *
RwFileViewGet (
  VOID
  );

/** Refill the entries of the current folder. **/
VOID
RwFileViewReload (
  VOID
  );

typedef struct _RW_FILE_EDIT RW_FILE_EDIT;

struct _RW_FILE_EDIT {
  RW_MDI_WINDOW     *Window;
  EFI_HANDLE        Vol;
  CHAR16            Path[RW_FILE_PATH_MAX];
  CHAR8             Name[64];         ///< file name for the title
  BOOLEAN           Text;             ///< text editor, else hex editor
  BOOLEAN           Ucs2;             ///< text file was UCS-2 LE with BOM (saved the same way)
  BOOLEAN           Dirty;
  // text
  lv_obj_t          *Area;            ///< lv_textarea
  RW_WIDGET         *Widget;
  // hex
  UINT8             *Data;
  UINTN             Size;
  RW_DATA_SOURCE    Source;
  RW_SPACE_PANE     Pane;
  RW_HEXGRID        *Grid;
  RW_FILE_EDIT      *Next;
};

/** Open Path in a new editor window (text when it looks like text). **/
RW_FILE_EDIT *
RwFileEditOpen (
  IN EFI_HANDLE    Vol,
  IN CONST CHAR16  *Path
  );

/** Editor of a window, or NULL. **/
RW_FILE_EDIT *
RwFileEditOf (
  IN RW_MDI_WINDOW  *Window
  );

/** Write the editor's buffer to its file (no question). **/
EFI_STATUS
RwFileEditSave (
  IN RW_FILE_EDIT  *Edit
  );

/** Text of a text editor (UTF-8, owned by the editor). **/
CONST CHAR8 *
RwFileEditGetText (
  IN RW_FILE_EDIT  *Edit
  );

/** Editors with unsaved changes. **/
UINTN
RwFileEditDirtyCount (
  VOID
  );

#endif
