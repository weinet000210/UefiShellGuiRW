/** @file
  File access for the tools: volumes, folders, whole-file read / write, and
  the save directory next to UefiShellGuiRW.efi.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Tools.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Library/DevicePathLib.h>

#define MAX_DIR_ENTRIES  4096

STATIC
EFI_STATUS
OpenRoot (
  IN  EFI_HANDLE         Vol,
  OUT EFI_FILE_PROTOCOL  **Root
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_STATUS                       Status;

  *Root  = NULL;
  Status = gBS->HandleProtocol (Vol, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Fs->OpenVolume (Fs, Root);
}

/**
  Info of an open file (FreePool).
**/
STATIC
VOID *
GetInfo (
  IN EFI_FILE_PROTOCOL  *File,
  IN EFI_GUID           *Guid
  )
{
  UINTN       Size;
  VOID        *Info;
  EFI_STATUS  Status;

  Size   = 0;
  Status = File->GetInfo (File, Guid, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return NULL;
  }

  Info = AllocateZeroPool (Size + sizeof (CHAR16));
  if ((Info != NULL) && EFI_ERROR (File->GetInfo (File, Guid, &Size, Info))) {
    FreePool (Info);
    Info = NULL;
  }

  return Info;
}

/**
  Directory of the running image on its volume ("\" when it is the root).
**/
STATIC
EFI_STATUS
ImageLocation (
  OUT EFI_HANDLE  *Vol,
  OUT CHAR16      *Dir,
  IN  UINTN       DirChars
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Image;
  EFI_DEVICE_PATH_PROTOCOL   *Node;
  UINTN                      Length;
  UINTN                      Index;
  CHAR16                     *Name;

  if (EFI_ERROR (gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image)) ||
      (Image->DeviceHandle == NULL))
  {
    return EFI_NOT_FOUND;
  }

  *Vol   = Image->DeviceHandle;
  Dir[0] = L'\0';
  for (Node = Image->FilePath; (Node != NULL) && !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) && (DevicePathSubType (Node) == MEDIA_FILEPATH_DP)) {
      Name = ((FILEPATH_DEVICE_PATH *)Node)->PathName;
      if ((Dir[0] != L'\0') && (Dir[StrLen (Dir) - 1] != L'\\') && (Name[0] != L'\\')) {
        StrCatS (Dir, DirChars, L"\\");
      }

      StrCatS (Dir, DirChars, Name);
    }
  }

  //
  // Keep the folder: cut after the last backslash.
  //
  Length = StrLen (Dir);
  for (Index = Length; Index > 0 && Dir[Index - 1] != L'\\'; Index--) {
  }

  Dir[Index] = L'\0';
  if (Dir[0] != L'\\') {
    StrCpyS (Dir, DirChars, L"\\");
  }

  return EFI_SUCCESS;
}

STATIC
VOID
FillVolume (
  IN  EFI_HANDLE  Handle,
  OUT RW_VOLUME   *Vol
  )
{
  EFI_FILE_PROTOCOL     *Root;
  EFI_FILE_SYSTEM_INFO  *Info;
  CHAR16                *Text;
  UINTN                 Length;
  UINTN                 Skip;

  ZeroMem (Vol, sizeof (*Vol));
  Vol->Handle = Handle;
  if (!EFI_ERROR (OpenRoot (Handle, &Root))) {
    Info = GetInfo (Root, &gEfiFileSystemInfoGuid);
    if (Info != NULL) {
      RwUnicodeToAscii (Info->VolumeLabel, Vol->Label, sizeof (Vol->Label));
      Vol->ReadOnly = Info->ReadOnly;
      Vol->Size     = Info->VolumeSize;
      Vol->Free     = Info->FreeSpace;
      FreePool (Info);
    }

    Root->Close (Root);
  }

  Text = ConvertDevicePathToText (DevicePathFromHandle (Handle), TRUE, TRUE);
  if (Text != NULL) {
    Length = StrLen (Text);
    Skip   = (Length + 1 > sizeof (Vol->Path)) ? Length + 1 - sizeof (Vol->Path) + 3 : 0;
    if (Skip > 0) {
      AsciiStrCpyS (Vol->Path, sizeof (Vol->Path), "...");
      RwUnicodeToAscii (Text + Skip, Vol->Path + 3, sizeof (Vol->Path) - 3);
    } else {
      RwUnicodeToAscii (Text, Vol->Path, sizeof (Vol->Path));
    }

    FreePool (Text);
  }
}

EFI_STATUS
RwVolumeList (
  OUT RW_VOLUME  **Vols,
  OUT UINTN      *Count
  )
{
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;
  UINTN       Out;
  EFI_HANDLE  ImageVol;
  CHAR16      Dir[RW_FILE_PATH_MAX];
  RW_VOLUME   Swap;
  EFI_STATUS  Status;

  *Vols  = NULL;
  *Count = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Vols = AllocateZeroPool (HandleCount * sizeof (RW_VOLUME));
  if (*Vols == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  ImageVol = NULL;
  ImageLocation (&ImageVol, Dir, ARRAY_SIZE (Dir));
  for (Index = 0, Out = 0; Index < HandleCount; Index++) {
    FillVolume (Handles[Index], &(*Vols)[Out]);
    (*Vols)[Out].IsImage = (BOOLEAN)(Handles[Index] == ImageVol);
    if ((*Vols)[Out].IsImage && (Out > 0)) {
      CopyMem (&Swap, &(*Vols)[0], sizeof (Swap));
      CopyMem (&(*Vols)[0], &(*Vols)[Out], sizeof (Swap));
      CopyMem (&(*Vols)[Out], &Swap, sizeof (Swap));
    }

    Out++;
  }

  FreePool (Handles);
  *Count = Out;
  return EFI_SUCCESS;
}

STATIC
INTN
NameCompare (
  IN CONST CHAR16  *A,
  IN CONST CHAR16  *B
  )
{
  CHAR16  Ca;
  CHAR16  Cb;

  for ( ; ; A++, B++) {
    Ca = ((*A >= L'a') && (*A <= L'z')) ? (CHAR16)(*A - 32) : *A;
    Cb = ((*B >= L'a') && (*B <= L'z')) ? (CHAR16)(*B - 32) : *B;
    if ((Ca != Cb) || (Ca == L'\0')) {
      return (INTN)Ca - (INTN)Cb;
    }
  }
}

STATIC
BOOLEAN
Before (
  IN CONST RW_DIR_ENTRY  *A,
  IN CONST RW_DIR_ENTRY  *B
  )
{
  if (A->IsDir != B->IsDir) {
    return A->IsDir;
  }

  return (BOOLEAN)(NameCompare (A->Name, B->Name) < 0);
}

EFI_STATUS
RwDirList (
  IN  EFI_HANDLE    Vol,
  IN  CONST CHAR16  *Dir,
  OUT RW_DIR_ENTRY  **Entries,
  OUT UINTN         *Count
  )
{
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *Folder;
  EFI_FILE_INFO      *Info;
  UINTN              Size;
  UINTN              Total;
  UINTN              Pos;
  RW_DIR_ENTRY       *List;
  RW_DIR_ENTRY       Item;
  EFI_STATUS         Status;

  *Entries = NULL;
  *Count   = 0;
  Status   = OpenRoot (Vol, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Folder = Root;
  if ((Dir != NULL) && (StrCmp (Dir, L"\\") != 0) && (Dir[0] != L'\0')) {
    Status = Root->Open (Root, &Folder, (CHAR16 *)Dir, EFI_FILE_MODE_READ, 0);
    Root->Close (Root);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  List = AllocateZeroPool (MAX_DIR_ENTRIES * sizeof (RW_DIR_ENTRY));
  Info = AllocateZeroPool (SIZE_4KB);
  if ((List == NULL) || (Info == NULL)) {
    Folder->Close (Folder);
    if (List != NULL) {
      FreePool (List);
    }

    if (Info != NULL) {
      FreePool (Info);
    }

    return EFI_OUT_OF_RESOURCES;
  }

  Total = 0;
  while (Total < MAX_DIR_ENTRIES) {
    Size   = SIZE_4KB;
    Status = Folder->Read (Folder, &Size, Info);
    if (EFI_ERROR (Status) || (Size == 0)) {
      break;
    }

    if ((StrCmp (Info->FileName, L".") == 0) || (StrCmp (Info->FileName, L"..") == 0)) {
      continue;
    }

    Item.Name  = AllocateCopyPool (StrSize (Info->FileName), Info->FileName);
    Item.Size  = Info->FileSize;
    Item.IsDir = (BOOLEAN)((Info->Attribute & EFI_FILE_DIRECTORY) != 0);
    CopyMem (&Item.Time, &Info->ModificationTime, sizeof (EFI_TIME));
    if (Item.Name == NULL) {
      break;
    }

    //
    // Insertion sort: folders first, then by name (directories are small).
    //
    for (Pos = Total; Pos > 0 && Before (&Item, &List[Pos - 1]); Pos--) {
      CopyMem (&List[Pos], &List[Pos - 1], sizeof (RW_DIR_ENTRY));
    }

    CopyMem (&List[Pos], &Item, sizeof (RW_DIR_ENTRY));
    Total++;
  }

  FreePool (Info);
  Folder->Close (Folder);
  *Entries = List;
  *Count   = Total;
  return EFI_SUCCESS;
}

VOID
RwDirFree (
  IN RW_DIR_ENTRY  *Entries,
  IN UINTN         Count
  )
{
  UINTN  Index;

  if (Entries == NULL) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (Entries[Index].Name != NULL) {
      FreePool (Entries[Index].Name);
    }
  }

  FreePool (Entries);
}

EFI_STATUS
RwFileRead (
  IN  EFI_HANDLE    Vol,
  IN  CONST CHAR16  *Path,
  IN  UINTN         MaxSize,
  OUT UINT8         **Data,
  OUT UINTN         *Size
  )
{
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_INFO      *Info;
  UINTN              Length;
  UINT8              *Buffer;
  EFI_STATUS         Status;

  *Data  = NULL;
  *Size  = 0;
  Status = OpenRoot (Vol, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Info = GetInfo (File, &gEfiFileInfoGuid);
  if (Info == NULL) {
    File->Close (File);
    return EFI_DEVICE_ERROR;
  }

  if ((Info->Attribute & EFI_FILE_DIRECTORY) != 0) {
    FreePool (Info);
    File->Close (File);
    return EFI_ACCESS_DENIED;
  }

  if (Info->FileSize > MaxSize) {
    *Size = (UINTN)MIN (Info->FileSize, MAX_UINTN);
    FreePool (Info);
    File->Close (File);
    return EFI_BAD_BUFFER_SIZE;
  }

  Length = (UINTN)Info->FileSize;
  FreePool (Info);
  Buffer = AllocateZeroPool (Length + 2);     // room for a terminator
  if (Buffer == NULL) {
    File->Close (File);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->Read (File, &Length, Buffer);
  File->Close (File);
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }

  *Data = Buffer;
  *Size = Length;
  return EFI_SUCCESS;
}

EFI_STATUS
RwFileWrite (
  IN EFI_HANDLE    Vol,
  IN CONST CHAR16  *Path,
  IN CONST VOID    *Data,
  IN UINTN         Size
  )
{
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  UINTN              Length;
  EFI_STATUS         Status;

  Status = OpenRoot (Vol, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Replace: delete an existing file first so no old tail stays behind.
  //
  if (!EFI_ERROR (Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0))) {
    File->Delete (File);
  }

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Length = Size;
  Status = File->Write (File, &Length, (VOID *)Data);
  if (!EFI_ERROR (Status) && (Length != Size)) {
    Status = EFI_VOLUME_FULL;
  }

  if (!EFI_ERROR (Status)) {
    Status = File->Flush (File);
  }

  File->Close (File);
  return Status;
}

BOOLEAN
RwFileExists (
  IN EFI_HANDLE    Vol,
  IN CONST CHAR16  *Path
  )
{
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  EFI_STATUS         Status;

  if (EFI_ERROR (OpenRoot (Vol, &Root))) {
    return FALSE;
  }

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  File->Close (File);
  return TRUE;
}

EFI_STATUS
RwSaveTarget (
  OUT EFI_HANDLE  *Vol,
  OUT CHAR16      *Dir,
  IN  UINTN       DirChars
  )
{
  RW_VOLUME  *Vols;
  UINTN      Count;
  UINTN      Index;

  if (EFI_ERROR (RwVolumeList (&Vols, &Count))) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < Count; Index++) {
    if (!Vols[Index].ReadOnly) {
      *Vol = Vols[Index].Handle;
      if (!Vols[Index].IsImage || EFI_ERROR (ImageLocation (Vol, Dir, DirChars))) {
        *Vol = Vols[Index].Handle;
        StrCpyS (Dir, DirChars, L"\\");
      }

      FreePool (Vols);
      return EFI_SUCCESS;
    }
  }

  FreePool (Vols);
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
SavePath (
  IN  CONST CHAR8  *FileName,
  OUT EFI_HANDLE   *Vol,
  OUT CHAR16       *Path,
  IN  UINTN        PathChars
  )
{
  EFI_STATUS  Status;
  UINTN       Length;

  Status = RwSaveTarget (Vol, Path, PathChars);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Length = StrLen (Path);
  if (Length + AsciiStrLen (FileName) + 1 > PathChars) {
    return EFI_BUFFER_TOO_SMALL;
  }

  AsciiStrToUnicodeStrS (FileName, Path + Length, PathChars - Length);
  return EFI_SUCCESS;
}

EFI_STATUS
RwSaveFindFree (
  IN  CONST CHAR8  *Prefix,
  IN  CONST CHAR8  *Ext1,
  IN  CONST CHAR8  *Ext2  OPTIONAL,
  OUT CHAR8        *Base,
  IN  UINTN        BaseSize
  )
{
  EFI_HANDLE  Vol;
  CHAR16      Dir[RW_FILE_PATH_MAX];
  CHAR16      Path[RW_FILE_PATH_MAX];
  UINTN       Number;
  EFI_STATUS  Status;

  Status = RwSaveTarget (&Vol, Dir, ARRAY_SIZE (Dir));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Number = 1; Number <= 9999; Number++) {
    UnicodeSPrint (Path, sizeof (Path), L"%s%a%04d.%a", Dir, Prefix, Number, Ext1);
    if (RwFileExists (Vol, Path)) {
      continue;
    }

    if (Ext2 != NULL) {
      UnicodeSPrint (Path, sizeof (Path), L"%s%a%04d.%a", Dir, Prefix, Number, Ext2);
      if (RwFileExists (Vol, Path)) {
        continue;
      }
    }

    AsciiSPrint (Base, BaseSize, "%a%04d", Prefix, Number);
    return EFI_SUCCESS;
  }

  return EFI_VOLUME_FULL;
}

EFI_STATUS
RwSaveWrite (
  IN CONST CHAR8  *FileName,
  IN CONST VOID   *Data,
  IN UINTN        Size
  )
{
  EFI_HANDLE  Vol;
  CHAR16      Path[RW_FILE_PATH_MAX];
  EFI_STATUS  Status;

  Status = SavePath (FileName, &Vol, Path, ARRAY_SIZE (Path));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return RwFileWrite (Vol, Path, Data, Size);
}

EFI_STATUS
RwSaveRead (
  IN  CONST CHAR8  *FileName,
  IN  UINTN        MaxSize,
  OUT UINT8        **Data,
  OUT UINTN        *Size
  )
{
  EFI_HANDLE  Vol;
  CHAR16      Path[RW_FILE_PATH_MAX];
  EFI_STATUS  Status;

  *Data  = NULL;
  *Size  = 0;
  Status = SavePath (FileName, &Vol, Path, ARRAY_SIZE (Path));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return RwFileRead (Vol, Path, MaxSize, Data, Size);
}

EFI_STATUS
RwSaveNext (
  IN  CONST CHAR8  *Prefix,
  IN  CONST CHAR8  *Ext,
  IN  CONST VOID   *Data,
  IN  UINTN        Size,
  OUT CHAR8        *Name,
  IN  UINTN        NameSize
  )
{
  CHAR8       Base[16];
  EFI_STATUS  Status;

  Name[0] = '\0';
  Status  = RwSaveFindFree (Prefix, Ext, NULL, Base, sizeof (Base));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  AsciiSPrint (Name, NameSize, "%a.%a", Base, Ext);
  return RwSaveWrite (Name, Data, Size);
}
